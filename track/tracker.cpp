#include <iostream>
#include <windows.h>
#include <string>
#include <ctime>
#include "sqlite3.h"

using namespace std;
int callback(void* data, int argc, char** argv, char** azColName);
int main()
{

    // CONNNECTING DATABASE(SQLLITE)
    sqlite3 *tracetime;
    int dbcont = sqlite3_open("tracetime.db", &tracetime);
    if (dbcont == SQLITE_OK)
    {
        // SQLLITE QUERY
        cout << "DATABASE CONNECTED SUCCESSFULLY\n";
        const string sql = R"(CREATE TABLE IF NOT EXISTS usage_sessions (
id INTEGER PRIMARY KEY AUTOINCREMENT,
app_name TEXT NOT NULL,
start_time INTEGER NOT NULL,
end_time INTEGER,
duration INTEGER
);

)";
        char* errMsg = nullptr;
        sqlite3_exec(tracetime,sql.c_str(),nullptr,nullptr,&errMsg);
        HWND temp;
        DWORD temp2;
        char processPath[1024];
        char previousPath[1024] = "";
        char *result;
        // debuging wheter 
        const string testquery = R"(SELECT name FROM sqlite_master WHERE type = 'table';)";
        sqlite3_exec(tracetime,testquery.c_str(),callback,nullptr,&errMsg);
        sqlite3_close(tracetime);
        // CHECKING WHICH PROCESS IS CURRENTLY RUNNING
        while (true)
        {
            time_t current_dt = time(0);
            // GETTING CURRENT TIME
            result = ctime(&current_dt);
            temp = GetForegroundWindow();
            GetWindowThreadProcessId(temp, &temp2);
            HANDLE temp3 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, temp2);
            if (temp3)
            {
                DWORD buff = 1024;
                if (QueryFullProcessImageNameA(temp3, 0, processPath, &buff))
                {
                    if (strcmp(processPath, previousPath))
                    {
                        string str(processPath);
                        int pos = str.find_last_of('\\');
                        str = str.substr(pos + 1);
                        cout << str << " " << result << '\n';
                        strcpy(previousPath, processPath);
                    }
                }
                CloseHandle(temp3);
            }
            Sleep(1000);
        }
    }
    else
        cout << sqlite3_errmsg(tracetime);
    return 0;
}

int callback(void* data, int argc, char** argv, char** azColName){
    for(int i = 0 ; i < argc ; i++){
        cout << azColName[i] << " || " << argv[i] << '\n';
    }
    return 0;
}