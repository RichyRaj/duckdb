#pragma once

#include <iostream>

typedef struct sql_result {
    bool error;
    string eMsg;
    string sql;
}TSQLResult;

class TigerDB {
public:
    TigerDB();
    void print();    
};