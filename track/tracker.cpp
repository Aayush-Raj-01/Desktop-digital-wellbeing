#include <iostream>
#include <windows.h>
#include <vector>
#include <algorithm>
#include <string>
#include <cstring>
#include <unordered_set>
#include <unordered_map>
// #include <time>
#include <ctime>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <atomic>
#include <chrono>
#include <tlhelp32.h>
#include "sqlite3.h"

using namespace std;

// MAINTAINNG CURRENT TIME IN A DB FOR RESCUE
void updateTrackerTime(atomic<bool> &running, mutex &timerMutex, condition_variable &timerCV)
{
    sqlite3 *timeDB = nullptr;
    if (sqlite3_open("tracetime.db", &timeDB) != SQLITE_OK)
    {
        cerr << "TimeStamo DB connection failed\n";
        return;
    }
    sqlite3_busy_timeout(timeDB, 3000);
    sqlite3_stmt *stmt = nullptr;

    const string upSeenQuery = "UPDATE tracker_state SET last_active_time = ? WHERE id = 1;";
    if (sqlite3_prepare_v2(timeDB, upSeenQuery.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    {
        cerr << "TimeStamp prepare failed: " << sqlite3_errmsg(timeDB) << '\n';
        sqlite3_close(timeDB);
        return;
    }
    while (running.load())
    {
        time_t currenttime = time(nullptr);
        sqlite3_bind_int64(stmt, 1, currenttime);
        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE)
        {
            cerr << "Timestamp update failed: " << sqlite3_errmsg(timeDB) << '\n';
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);

        unique_lock<mutex> lock(timerMutex);

        timerCV.wait_for(lock, chrono::seconds(1), [&running]()
                         { return !running.load(); });
    }
    sqlite3_finalize(stmt);
    sqlite3_close(timeDB);
}

// recovery  SYSTEM
void recoveryPreviousSessions(sqlite3 *tracetime)
{
    const char *query = R"(
        UPDATE usage_sessions
        SET
            end_time = (
                SELECT last_active_time
                FROM tracker_state
                WHERE id = 1
            ),
            duration = MAX(0, (
                SELECT last_active_time
                FROM tracker_state
                WHERE id = 1
            ) - start_time),
            is_active = 0
        WHERE is_active = 1
        AND EXISTS (
            SELECT 1
            FROM tracker_state
            WHERE id = 1 AND last_active_time > 0
        );
    )";

    char *errMsg = nullptr;

    int rc = sqlite3_exec(
        tracetime, query, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK)
    {
        cerr << "Recovery failed: " << errMsg << '\n';
        sqlite3_free(errMsg);
        return;
    }

    cout << "Recovered Sessions: "
         << sqlite3_changes(tracetime) << '\n';
}

// function to create a new db
void initializeTrackerState(sqlite3 *tracetime)
{
    const char *query = R"(
        CREATE TABLE IF NOT EXISTS tracker_state (
            id INTEGER PRIMARY KEY CHECK(id = 1),
            last_active_time INTEGER NOT NULL
        );

        INSERT OR IGNORE INTO tracker_state
        (id, last_active_time)
        VALUES (1, 0);
    )";

    char *errMsg = nullptr;

    int rc = sqlite3_exec(
        tracetime, query, nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK)
    {
        cerr << "Tracker state initialization failed: "
             << errMsg << '\n';
        sqlite3_free(errMsg);
    }
    else
    {
        cout << "Tracker state initialized\n";
    }
}

// Function when user exits the program;
void finalizeActiveSessions(sqlite3 *tracetime)
{
    time_t currenttime = time(nullptr);

    const char *query = R"(
    UPDATE uasge_sessions
    SET
        end_time =?,
        durtion = MAX(0, ? - start_time),
        is_acctive = 0
    WHERE is_active = 1;
    )";

    sqlite3_stmt *stmt = nullptr;

    int rc = sqlite3_prepare_v2(tracetime, query, -1, &stmt, nullptr);

    if (rc != SQLITE_OK)
    {
        cerr << "Shutdown Preparation failed: " << sqlite3_errmsg(tracetime) << '\n';
        return;
    }

    sqlite3_bind_int64(stmt, 1, currenttime);
    sqlite3_bind_int64(stmt, 2, currenttime);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
    {
        cout << "Finalized sessions: "
             << sqlite3_changes(tracetime) << '\n';
    }
    else
    {
        cerr << "Shutdown update failed: "
             << sqlite3_errmsg(tracetime) << '\n';
    }
}


int callback(void *data, int argc, char **argv, char **azColName);
int main()
{
    // Opening Database
    sqlite3 *tracetime;
    int dbcont = sqlite3_open("tracetime.db", &tracetime);
    atomic<bool> running(true);
    mutex timerMutex;
    condition_variable timerCV;
    // Creating tracker state db
    initializeTrackerState(tracetime);
    // Calling recovery
    recoveryPreviousSessions(tracetime);
    // starting the timer
    thread timerThread(updateTrackerTime, ref(running), ref(timerMutex), ref(timerCV));

    vector<string> ignoredProcesses = {
        "svchost.exe",
        "services.exe",
        "lsass.exe",
        "wininit.exe",
        "csrss.exe",
        "smss.exe"};

        // CONNNECTING DATABASE(SQLLITE)
        if (dbcont == SQLITE_OK)
        {
            // SQLLITE QUERY
            cout << "DATABASE CONNECTED SUCCESSFULLY\n";
            const string sql = R"(CREATE TABLE IF NOT EXISTS usage_sessions (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
app_name TEXT NOT NULL,
process_id INTEGER NOT NULL,
start_time INTEGER NOT NULL,
end_time INTEGER,
duration INTEGER
);

)";
char *errMsg = nullptr;
sqlite3_exec(tracetime, sql.c_str(), nullptr, nullptr, &errMsg);
HWND temp;
        DWORD temp2;
        char processPath[1024];
        char previousPath[1024] = "";
        char *result;
        // debuging wheter
        const string testquery = R"(SELECT name FROM sqlite_master WHERE type = 'table';)";
        sqlite3_exec(tracetime, testquery.c_str(), callback, nullptr, &errMsg);
        // creating a db for storing time
        const string timequery = R"(CREATE TABLE IF NOT EXISTS tracker_state(
            id INTEGER PRIMARy KEY CHECK(id = 1),
            last_active_time INTEGER NOT NULL
            );)";
            
            // structure for saving the process
            struct ProcessInfo{
                string name;
                FILETIME creationTime;
            };
            unordered_map<DWORD, ProcessInfo> CurrentProcess;
            unordered_map<DWORD, ProcessInfo> PreviousProcess;
            // getting foreground process
            while (true)
            {
            // cout << "\n========== NEW SCAN ==========\n";
            time_t current_dt = time(0);
            HANDLE proce = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            PROCESSENTRY32 pe;
            pe.dwSize = sizeof(PROCESSENTRY32);
            if (proce == INVALID_HANDLE_VALUE)
            cout << "FAILED TO GET THE PROCESS";
            else
            {

                if (Process32First(proce, &pe))
                {
                    CurrentProcess.clear();
                    do
                    {
                        string exefilename(pe.szExeFile);
                        if (find(ignoredProcesses.begin(), ignoredProcesses.end(), exefilename) == ignoredProcesses.end())
                        {
                            // filling the currentprocess set with the process
                            HANDLE hProcess = OpenProcess(
                                PROCESS_QUERY_LIMITED_INFORMATION,
                                FALSE,
                                pe.cntUsage
                            );
                            if(hProcess != nullptr){
                                FILETIME creationTime , exitTime, kernelTime , userTime;
                                if(GetProcessTimes(hProcess,&creationTime,&exitTime,&kernelTime,&userTime)){
                                    CurrentProcess[pe.th32ProcessID] = {
                                        exefilename,
                                        creationTime
                                    };
                                }
                                CloseHandle(hProcess);
                            }
                        }
                    
                    } while (Process32Next(proce, &pe));
                }
                CloseHandle(proce);
            }
            // ------debug for testing-----------
            // cout << "\n========== CURRENT PROCESSES ==========\n";

            // for (const auto &proc : CurrentProcess)
            // {
                //     cout << "PID: " << proc.first
                //          << " | APP: " << proc.second << '\n';
                // }
                
                // comparing previous and current process;
            for (const auto &procce : CurrentProcess)
            {
                auto prev = PreviousProcess.find(procce.first);
                // NEW PROCESS
                if (prev == PreviousProcess.end())
                {
                    // debug line for testing -----------------------
                    // cout << "[NEW PROCESS]" << procce.second << " | PID :" << procce.first << '\n';

                    time_t currenttime = time(nullptr);
                    const string addsqlquery = R"(INSERT INTO usage_sessions(app_name,process_id,start_time,is_active)
                    VALUES(?,?,?,1))";
                    sqlite3_stmt *stmt;
                    int rc = sqlite3_prepare_v2(tracetime, addsqlquery.c_str(), -1, &stmt, nullptr);
                    if (rc == SQLITE_OK)
                    {

                        sqlite3_bind_text(stmt, 1, procce.second.name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(stmt, 2, procce.first);
                        sqlite3_bind_int64(stmt, 3, currenttime);

                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
                else if (prev->second.creationTime.dwHighDateTime != procce.second.creationTime.dwHighDateTime || prev->second.creationTime.dwLowDateTime != procce.second.creationTime.dwLowDateTime)
                {

                    // saving the details ofnnthe previous stoped process
                    time_t endtime = time(nullptr);
                    const string closeQuery = R"(
                    UPDATE usage_sessions
                    SET end_time = ?,
                        duration = MAX(0, ? - start_time),
                        is_active = 0
                    WHERE process_id = ?
                    AND is_active = 1;
                )";

                sqlite3_stmt* stmt;
                if(sqlite3_prepare_v2(tracetime,closeQuery.c_str(),-1,&stmt,nullptr) == SQLITE_OK){
                    sqlite3_bind_int(stmt,1,endtime);
                    sqlite3_bind_int64(stmt,2,endtime);
                    sqlite3_bind_int64(stmt,3,procce.first);

                    sqlite3_step(stmt);
                    sqlite3_finalize(stmt);
                }
                // add new preocess

                time_t startTime = time(nullptr);

                const string insertQuery = R"(
                INSERT INTO usage_sessions(app_name,process_id,start_time,is_active)
                VALUES(?,?,?,1);
                )";


                sqlite3_stmt* insertStmt;

                if(sqlite3_prepare_v2(tracetime,insertQuery.c_str(),-1,&insertStmt,nullptr) == SQLITE_OK){
                    sqlite3_bind_text(insertStmt,1,procce.second.name.c_str(),-1,SQLITE_TRANSIENT);
                    sqlite3_bind_int64(insertStmt,2,procce.first);  
                    sqlite3_bind_int64(insertStmt,3,startTime);  
                }

                }
            }
            for (const auto &procce : PreviousProcess)
            {
                // EXITED PROCESS
                if (CurrentProcess.find(procce.first) == CurrentProcess.end())
                {
                    // debug testing have to remove later --------------------------
                    // cout<< "[eXIteD process]" << procce.second << " | PID:" << procce.first << '\n';
                    // procce.first  = PID
                    // procce.second = app name
                    time_t endtime = time(nullptr);
                    const string updatesqlquery = R"(UPDATE usage_sessions
                    SET end_time = ?,
                    duration = ? - start_time
                    WHERE process_id = ?
                    AND end_time IS NULL;)";

                    sqlite3_stmt *stmt;
                    if (sqlite3_prepare_v2(tracetime, updatesqlquery.c_str(), -1, &stmt, nullptr) == SQLITE_OK)
                    {
                        sqlite3_bind_int64(stmt, 1, endtime);
                        sqlite3_bind_int64(stmt, 2, endtime);
                        sqlite3_bind_int64(stmt, 3, procce.first);
                        sqlite3_step(stmt);
                        sqlite3_finalize(stmt);
                    }
                }
                else
                {
                    // debug testing have to remove later --------------------------
                    // cout << "[RUNNING]" << procce.second << " | PID:" <<  procce.first << '\n';
                }
            }
            // cout << "\n========== SCAN COMPLETE ==========\n";
            PreviousProcess = CurrentProcess;

            // CHECKING WHICH PROCESS IS CURRENTLY RUNNING
            // GETTING CURRENT TIME
            // o-------------------------------------------------------------o
            // result = ctime(&current_dt);
            // temp = GetForegroundWindow();
            // GetWindowThreadProcessId(temp, &temp2);
            // HANDLE temp3 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, temp2);
            // if (temp3)
            // {
            //     DWORD buff = 1024;
            //     if (QueryFullProcessImageNameA(temp3, 0, processPath, &buff))
            //     {
            //         if (strcmp(processPath, previousPath))
            //         {
            //             string str(processPath);
            //             int pos = str.find_last_of('\\');
            //             str = str.substr(pos + 1);
            //             cout << str << " " << result << '\n';
            //             strcpy(previousPath, processPath);
            //         }
            //     }
            //     CloseHandle(temp3);
            // }
            Sleep(1000);
        }
        sqlite3_close(tracetime);
    }
    else
        cout << sqlite3_errmsg(tracetime);
    return 0;
}

int callback(void *data, int argc, char **argv, char **azColName)
{
    for (int i = 0; i < argc; i++)
    {
        cout << azColName[i] << " || " << argv[i] << '\n';
    }
    return 0;
}