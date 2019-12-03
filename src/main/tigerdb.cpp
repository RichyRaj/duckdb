#include "duckdb/main/tigerdb.hpp"

TSQLResult TigerDB::parseTableName(vector<string>& t) {
    TSQLResult tr;
    tr.error = false;
    tr.eMsg = "None";
    tr.sql = "";
    tr.stopSQLFlow = true;
    if (t.size() != 4) {
        tr.error = true;
        tr.eMsg = "TQL Error: Invalid SET statement";
        return tr;
    }
    if (t[1] == "t1" || t[1] == "T1") {
        t1 = t[3];
    } else if (t[1] == "t2" || t[1] == "T2") {
        t2 = t[3];
    } else {
        tr.error = true;
        tr.eMsg = "TQL Error: Invalid SET statement";
    }
    return tr;
}

TSQLResult TigerDB::parseShow(vector<string>& t) {
    TSQLResult tr;
    tr.error = false;
    tr.eMsg = "None";
    tr.sql = "";
    tr.stopSQLFlow = false;

    string prefix = "SELECT * FROM ";
    string tab = "";

    // Right Now, Show will only support Basic Show
    // [0] is show
    if (t[1] == "DATA" || t[1] == "data") {
        // t2 will be shown
        tr.sql += prefix + t2;
    } else if (t[1] == "META" || t[1] == "meta") {
        // t1 will be shown
        tr.sql += prefix + t1;
    } else {
        // TODO
        tr.sql += prefix + t2;
    }

    return tr;
} 

TSQLResult TigerDB::parse(string tql) {
    TSQLResult t;
    t.error = false;
    t.eMsg = "None";
    t.sql = "";
    t.stopSQLFlow = false;
    // Vector of string to save tokens 
    vector<string> tokens; 
      
    stringstream check1(tql);      
    string intermediate; 
      
    while(getline(check1, intermediate, ' ')) 
    { 
        tokens.push_back(intermediate); 
    } 

    if (tokens[0] == "CREATE" || tokens[0] == "create") {
        t.sql = tql;
    } else if (tokens[0] == "INSERT" || tokens[0] == "insert") {
        t.sql = tql;
    } else if (tokens[0] == "SET" || tokens[0] == "set") {
        // Parse
        return this->parseTableName(tokens);
    } else if (tokens[0] == "SHOW" || tokens[0] == "show") {
        // Parse
        if (t1 == "" || t2 == "") {
            t.error = true;
            t.eMsg = "TQL Query Error: You have not defined Table 1 and Table 2 Yet"; 
            return t;
        }
        // Valid show
        return this->parseShow(tokens);

    } else {
        t.error = true;
        t.eMsg = "TigerDB V0.1 only supports SHOW TQL queries and CREATE and INSERT SQL queries";
    }

    return t;
}