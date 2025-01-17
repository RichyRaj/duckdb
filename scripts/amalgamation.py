# this script creates a single header + source file combination out of the DuckDB sources
import os, re, sys, pickle
amal_dir = os.path.join('src', 'amalgamation')
header_file = os.path.join(amal_dir, "duckdb.hpp")
source_file = os.path.join(amal_dir, "duckdb.cpp")

src_dir = 'src'
include_dir = os.path.join('src', 'include')
hll_dir = os.path.join('third_party', 'hyperloglog')
miniz_dir = os.path.join('third_party', 'miniz')
re2_dir = os.path.join('third_party', 're2')
pg_query_dir = os.path.join('third_party', 'libpg_query')
pg_query_include_dir = os.path.join('third_party', 'libpg_query', 'include')

# files included in the amalgamated "duckdb.hpp" file
main_header_files = [os.path.join(include_dir, 'duckdb.hpp'), os.path.join(include_dir, 'duckdb.h'), os.path.join(include_dir, 'duckdb', 'common', 'types', 'date.hpp'), os.path.join(include_dir, 'duckdb', 'common', 'types', 'timestamp.hpp')]

# include paths for where to search for include files during amalgamation
include_paths = [include_dir, hll_dir, re2_dir, miniz_dir, pg_query_include_dir, pg_query_dir]
# paths of where to look for files to compile and include to the final amalgamation
compile_directories = [src_dir, hll_dir, miniz_dir, re2_dir, pg_query_dir]

# files excluded from the amalgamation
excluded_files = ['grammar.cpp', 'grammar.hpp', 'symbols.cpp', 'file_system.cpp']
# files excluded from individual file compilation during test_compile
excluded_compilation_files = excluded_files + ['gram.hpp', 'kwlist.hpp', "duckdb-c.cpp"]
# where to cache which files have already been compiled, only used for --compile --resume
cache_file = 'amalgamation.cache'


linenumbers = False
compile = False
resume = False

for arg in sys.argv:
	if arg == '--compile':
		compile = True
	elif arg == '--resume':
		resume = True
	elif arg == '--linenumbers':
		linenumbers = True
	elif arg == '--no-linenumbers':
		linenumbers = False

if not resume:
	try:
		os.remove(cache_file)
	except:
		pass

def get_includes(fpath, text):
	# find all the includes referred to in the directory
	include_statements = re.findall("(^[#]include[\t ]+[\"]([^\"]+)[\"])", text, flags=re.MULTILINE)
	include_files = []
	# figure out where they are located
	for included_file in [x[1] for x in include_statements]:
		included_file = os.sep.join(included_file.split('/'))
		found = False
		for include_path in include_paths:
			ipath = os.path.join(include_path, included_file)
			if os.path.isfile(ipath):
				include_files.append(ipath)
				found = True
				break
		if not found:
			raise Exception('Could not find include file "' + included_file + '", included from file "' + fpath + '"')
	return ([x[0] for x in include_statements], include_files)

def cleanup_file(text):
	# remove all "#pragma once" notifications
	text = re.sub('#pragma once', '', text)
	return text

# recursively get all includes and write them
written_files = {}

def write_file(current_file, ignore_excluded = False):
	global linenumbers
	global written_files
	if current_file.split(os.sep)[-1] in excluded_files and not ignore_excluded:
		# file is in ignored files set
		return ""
	if current_file in written_files:
		# file is already written
		return ""
	written_files[current_file] = True

	# first read this file
	with open(current_file, 'r') as f:
		text = f.read()

	(statements, includes) = get_includes(current_file, text)
	# find the linenr of the final #include statement we parsed
	if len(statements) > 0:
		index = text.find(statements[-1])
		linenr = len(text[:index].split('\n'))

		# now write all the dependencies of this header first
		for i in range(len(includes)):
			include_text = write_file(includes[i])
			if linenumbers and i == len(includes) - 1:
				# for the last include statement, we also include a #line directive
				include_text += '\n#line %d "%s"\n' % (linenr, current_file)
			text = text.replace(statements[i], include_text)

	# add the initial line here
	if linenumbers:
		text = '#line 1 "%s"\n' % (current_file,) + text
	print(current_file)
	# now read the header and write it
	return cleanup_file(text)

def try_compilation(fpath, cache):
	if fpath in cache:
		return
	print(fpath)

	cmd = 'clang++ -std=c++11 -Wno-deprecated -Wno-writable-strings -S -MMD -MF dependencies.d -o deps.s ' + fpath + ' ' + ' '.join(["-I" + x for x in include_paths])
	ret = os.system(cmd)
	if ret != 0:
		raise Exception('Failed compilation of file "' + fpath + '"!\n Command: ' + cmd)
	cache[fpath] = True
	with open(cache_file, 'wb') as cf:
		pickle.dump(cache, cf)

def compile_dir(dir, cache):
	files = os.listdir(dir)
	files.sort()
	for fname in files:
		if fname in excluded_compilation_files:
			continue
		fpath = os.path.join(dir, fname)
		if os.path.isdir(fpath):
			compile_dir(fpath, cache)
		elif fname.endswith('.cpp') or fname.endswith('.hpp') or fname.endswith('.c') or fname.endswith('.cc'):
			try_compilation(fpath, cache)

if compile:
	# compilation pass only
	# compile all files in the src directory (including headers!) individually
	try:
		with open(cache_file, 'rb') as cf:
			cache = pickle.load(cf)
	except:
		cache = {}
	for cdir in compile_directories:
		compile_dir(cdir, cache)
	exit(0)


if not os.path.exists(amal_dir):
	os.makedirs(amal_dir)

# now construct duckdb.hpp from these headers
print("-----------------------")
print("-- Writing duckdb.hpp --")
print("-----------------------")
with open(header_file, 'w+') as hfile:
	hfile.write("#pragma once\n")
	for fpath in main_header_files:
		hfile.write(write_file(fpath))

def write_dir(dir, sfile):
	files = os.listdir(dir)
	files.sort()
	for fname in files:
		if fname in excluded_files:
			continue
		fpath = os.path.join(dir, fname)
		if os.path.isdir(fpath):
			write_dir(fpath, sfile)
		elif fname.endswith('.cpp') or fname.endswith('.c') or fname.endswith('.cc'):
			sfile.write(write_file(fpath))

# now construct duckdb.cpp
print("------------------------")
print("-- Writing duckdb.cpp --")
print("------------------------")

# scan all the .cpp files
with open(source_file, 'w+') as sfile:
	sfile.write('#include "duckdb.hpp"\n\n')
	for compile_dir in compile_directories:
		write_dir(compile_dir, sfile)
	# for windows we write file_system.cpp last
	# this is because it includes windows.h which contains a lot of #define statements that mess up the other code
	sfile.write(write_file(os.path.join('src', 'common', 'file_system.cpp'), True))
