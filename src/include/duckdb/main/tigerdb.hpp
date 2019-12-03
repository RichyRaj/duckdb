#pragma once

#include <iostream>
#include <vector>
#include <sstream>

using namespace std;

typedef struct sql_result {
    bool error;
    string eMsg;
    string sql;
    bool stopSQLFlow;
}TSQLResult;

class TigerDB {
private:
    string t1;
    string t2;
    TSQLResult parseTableName(vector<string>&);
    TSQLResult parseShow(vector<string>&);
public:
    TigerDB() : t1(""), t2("") {};
    TSQLResult parse(string);    
};