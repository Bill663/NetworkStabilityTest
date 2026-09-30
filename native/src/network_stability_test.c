#define _UNICODE
#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <iphlpapi.h>
#include <icmpapi.h>
#include <wlanapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>

#define APP_TITLE L"Network Stability Test"
#define WM_APP_UPDATE (WM_APP + 1)
#define WM_APP_DONE   (WM_APP + 2)
#define PROBE_COUNT 7

enum {
    IDC_DURATION = 100,
    IDC_UNIT,
    IDC_START,
    IDC_STOP,
    IDC_REPORTS,
    IDC_LOCATION,
    IDC_PROGRESS,
    IDC_REMAINING,
    IDC_NET_SCORE,
    IDC_NET_GRADE,
    IDC_NET_DETAIL,
    IDC_WIFI_SCORE,
    IDC_WIFI_GRADE,
    IDC_WIFI_DETAIL,
    IDC_PROBES,
    IDC_SUMMARY
};

typedef enum ProbeKind {
    PROBE_PING,
    PROBE_DNS,
    PROBE_TCP
} ProbeKind;

typedef struct ProbeDefinition {
    const char *layer;
    const char *name;
    const char *target;
    ProbeKind kind;
} ProbeDefinition;

typedef struct ProbeStats {
    unsigned sent;
    unsigned failed;
    double totalLatency;
    double minLatency;
    double maxLatency;
    double lastLatency;
    BOOL lastSuccess;
    char lastStatus[96];
} ProbeStats;

typedef struct WifiInfo {
    BOOL adapterPresent;
    BOOL connected;
    BOOL signalAvailable;
    BOOL permissionBlocked;
    int signal;
    int estimatedDbm;
    ULONG rxMbps;
    ULONG txMbps;
    WCHAR ssid[DOT11_SSID_MAX_LENGTH + 1];
    WCHAR adapter[128];
    WCHAR detail[256];
} WifiInfo;

typedef struct AppState {
    CRITICAL_SECTION lock;
    HWND hwnd;
    HANDLE worker;
    volatile LONG stopRequested;
    BOOL running;
    BOOL closing;
    BOOL stopped;
    int durationSeconds;
    ULONGLONG startTick;
    ULONGLONG elapsedMs;
    char gateway[64];
    ProbeStats probes[PROBE_COUNT];
    WifiInfo wifi;
    WCHAR reportDir[MAX_PATH];
    WCHAR csvPath[MAX_PATH];
    WCHAR reportPath[MAX_PATH];
} AppState;

typedef struct UiControls {
    HWND duration;
    HWND unit;
    HWND start;
    HWND stop;
    HWND reports;
    HWND location;
    HWND progress;
    HWND remaining;
    HWND netScore;
    HWND netGrade;
    HWND netDetail;
    HWND wifiScore;
    HWND wifiGrade;
    HWND wifiDetail;
    HWND probes;
    HWND summary;
} UiControls;

static AppState g_state;
static UiControls g_ui;
static HFONT g_font;
static HFONT g_titleFont;
static HFONT g_scoreFont;
static HBRUSH g_background;

static const ProbeDefinition g_definitions[PROBE_COUNT] = {
    {"Router",  "router-ping",       "Default gateway", PROBE_PING},
    {"Internet","cloudflare-ping",   "1.1.1.1",         PROBE_PING},
    {"Internet","google-ping",       "8.8.8.8",         PROBE_PING},
    {"DNS",     "dns-google",        "www.google.com",  PROBE_DNS},
    {"DNS",     "dns-cloudflare",    "www.cloudflare.com", PROBE_DNS},
    {"TCP",     "tcp-cloudflare-443","1.1.1.1:443",     PROBE_TCP},
    {"TCP",     "tcp-google-443",    "www.google.com:443", PROBE_TCP}
};

static void copy_wide(WCHAR *dest, size_t count, const WCHAR *source) {
    if (!count) return;
    wcsncpy(dest, source ? source : L"", count - 1);
    dest[count - 1] = L'\0';
}

static void copy_narrow(char *dest, size_t count, const char *source) {
    if (!count) return;
    strncpy(dest, source ? source : "", count - 1);
    dest[count - 1] = '\0';
}

static const WCHAR *grade_for_score(int score) {
    if (score < 0) return L"Waiting";
    if (score >= 90) return L"Excellent";
    if (score >= 75) return L"Good";
    if (score >= 55) return L"Fair";
    if (score >= 35) return L"Poor";
    return L"Critical";
}

static const WCHAR *wifi_grade(int signal) {
    if (signal >= 80) return L"Excellent";
    if (signal >= 60) return L"Good";
    if (signal >= 40) return L"Fair";
    if (signal >= 20) return L"Weak";
    return L"Critical";
}

static int latency_component(double averageMs, BOOL available) {
    if (!available) return 70;
    if (averageMs <= 40.0) return 100;
    if (averageMs <= 80.0) return 85;
    if (averageMs <= 150.0) return 65;
    if (averageMs <= 300.0) return 40;
    return 15;
}

static int calculate_score(const ProbeStats *stats, const WifiInfo *wifi,
                           double *lossPercent, double *averageLatency) {
    unsigned sent = 0;
    unsigned failed = 0;
    unsigned latencySamples = 0;
    double latencyTotal = 0.0;
    int i;

    for (i = 0; i < PROBE_COUNT; ++i) {
        sent += stats[i].sent;
        failed += stats[i].failed;
        if (i > 0) {
            unsigned successes = stats[i].sent - stats[i].failed;
            latencySamples += successes;
            latencyTotal += stats[i].totalLatency;
        }
    }
    if (!sent) {
        *lossPercent = 0.0;
        *averageLatency = 0.0;
        return -1;
    }

    *lossPercent = ((double)failed / (double)sent) * 100.0;
    *averageLatency = latencySamples ? latencyTotal / (double)latencySamples : 0.0;
    {
        double availability = 100.0 - (*lossPercent * 8.0);
        int latency = latency_component(*averageLatency, latencySamples > 0);
        double score;
        if (availability < 0.0) availability = 0.0;
        if (wifi->connected && wifi->signalAvailable) {
            score = availability * 0.55 + latency * 0.25 + wifi->signal * 0.20;
        } else {
            score = availability * 0.70 + latency * 0.30;
        }
        if (score < 0.0) score = 0.0;
        if (score > 100.0) score = 100.0;
        return (int)(score + 0.5);
    }
}

static void format_duration(WCHAR *buffer, size_t count, ULONGLONG seconds) {
    ULONGLONG hours = seconds / 3600;
    ULONGLONG minutes = (seconds % 3600) / 60;
    ULONGLONG remain = seconds % 60;
    if (hours) swprintf(buffer, count, L"%llu:%02llu:%02llu", hours, minutes, remain);
    else swprintf(buffer, count, L"%02llu:%02llu", minutes, remain);
}

static BOOL ensure_report_directory(void) {
    WCHAR base[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE, NULL,
                         SHGFP_TYPE_CURRENT, base) != S_OK) return FALSE;
    swprintf(g_state.reportDir, MAX_PATH, L"%ls\\NetworkStabilityTest", base);
    CreateDirectoryW(g_state.reportDir, NULL);
    wcscat(g_state.reportDir, L"\\reports");
    CreateDirectoryW(g_state.reportDir, NULL);
    return GetFileAttributesW(g_state.reportDir) != INVALID_FILE_ATTRIBUTES;
}

static BOOL ensure_report_paths(void) {
    SYSTEMTIME now;
    if (!ensure_report_directory()) return FALSE;
    GetLocalTime(&now);
    swprintf(g_state.csvPath, MAX_PATH,
             L"%ls\\network-%04u%02u%02u-%02u%02u%02u.csv",
             g_state.reportDir, now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
    swprintf(g_state.reportPath, MAX_PATH,
             L"%ls\\network-%04u%02u%02u-%02u%02u%02u-report.txt",
             g_state.reportDir, now.wYear, now.wMonth, now.wDay,
             now.wHour, now.wMinute, now.wSecond);
    return TRUE;
}

static BOOL discover_gateway(char *buffer, size_t count) {
    MIB_IPFORWARDROW row;
    IN_ADDR address;
    ZeroMemory(&row, sizeof(row));
    if (GetBestRoute(inet_addr("1.1.1.1"), 0, &row) != NO_ERROR ||
        row.dwForwardNextHop == 0) return FALSE;
    address.S_un.S_addr = row.dwForwardNextHop;
    return InetNtopA(AF_INET, &address, buffer, (DWORD)count) != NULL;
}

static BOOL ping_target(const char *target, double *latency, char *status, size_t statusCount) {
    HANDLE icmp = IcmpCreateFile();
    IN_ADDR address;
    char sendData[24] = "NetworkStabilityTest";
    unsigned char replyBuffer[sizeof(ICMP_ECHO_REPLY) + sizeof(sendData) + 32];
    DWORD result;
    if (icmp == INVALID_HANDLE_VALUE) {
        snprintf(status, statusCount, "ICMP unavailable (%lu)", GetLastError());
        return FALSE;
    }
    if (InetPtonA(AF_INET, target, &address) != 1) {
        IcmpCloseHandle(icmp);
        snprintf(status, statusCount, "Invalid address");
        return FALSE;
    }
    ZeroMemory(replyBuffer, sizeof(replyBuffer));
    result = IcmpSendEcho(icmp, address.S_un.S_addr, sendData, (WORD)strlen(sendData),
                          NULL, replyBuffer, sizeof(replyBuffer), 2000);
    if (result > 0) {
        PICMP_ECHO_REPLY reply = (PICMP_ECHO_REPLY)replyBuffer;
        *latency = (double)reply->RoundTripTime;
        snprintf(status, statusCount, "Reply");
        IcmpCloseHandle(icmp);
        return TRUE;
    }
    snprintf(status, statusCount, "No reply (%lu)", GetLastError());
    IcmpCloseHandle(icmp);
    return FALSE;
}

static BOOL dns_target(const char *target, double *latency, char *status, size_t statusCount) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    ULONGLONG started = GetTickCount64();
    int code;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    code = getaddrinfo(target, NULL, &hints, &result);
    *latency = (double)(GetTickCount64() - started);
    if (code == 0 && result) {
        freeaddrinfo(result);
        snprintf(status, statusCount, "Resolved");
        return TRUE;
    }
    if (result) freeaddrinfo(result);
    snprintf(status, statusCount, "DNS error (%d)", code);
    return FALSE;
}

static BOOL tcp_target(const char *host, const char *port, double *latency,
                       char *status, size_t statusCount) {
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *current;
    int code;
    BOOL success = FALSE;
    ULONGLONG started = GetTickCount64();

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    code = getaddrinfo(host, port, &hints, &addresses);
    if (code != 0) {
        snprintf(status, statusCount, "Resolve error (%d)", code);
        *latency = (double)(GetTickCount64() - started);
        return FALSE;
    }

    for (current = addresses; current && !success; current = current->ai_next) {
        SOCKET socketHandle = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        u_long nonBlocking = 1;
        if (socketHandle == INVALID_SOCKET) continue;
        ioctlsocket(socketHandle, FIONBIO, &nonBlocking);
        code = connect(socketHandle, current->ai_addr, (int)current->ai_addrlen);
        if (code == 0) {
            success = TRUE;
        } else if (WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set writeSet;
            fd_set errorSet;
            struct timeval timeout;
            int socketError = 0;
            int socketErrorSize = sizeof(socketError);
            FD_ZERO(&writeSet);
            FD_ZERO(&errorSet);
            FD_SET(socketHandle, &writeSet);
            FD_SET(socketHandle, &errorSet);
            timeout.tv_sec = 2;
            timeout.tv_usec = 0;
            code = select(0, NULL, &writeSet, &errorSet, &timeout);
            if (code > 0 && FD_ISSET(socketHandle, &writeSet) &&
                getsockopt(socketHandle, SOL_SOCKET, SO_ERROR,
                           (char *)&socketError, &socketErrorSize) == 0 && socketError == 0) {
                success = TRUE;
            }
        }
        closesocket(socketHandle);
    }
    freeaddrinfo(addresses);
    *latency = (double)(GetTickCount64() - started);
    snprintf(status, statusCount, success ? "Connected" : "Connection failed");
    return success;
}

static void query_wifi(WifiInfo *wifi) {
    HANDLE client = NULL;
    DWORD negotiated = 0;
    DWORD code;
    PWLAN_INTERFACE_INFO_LIST interfaces = NULL;
    DWORD i;
    ZeroMemory(wifi, sizeof(*wifi));
    copy_wide(wifi->detail, 256, L"Checking Windows wireless status.");

    code = WlanOpenHandle(2, NULL, &negotiated, &client);
    if (code != ERROR_SUCCESS) {
        wifi->permissionBlocked = (code == ERROR_ACCESS_DENIED);
        copy_wide(wifi->detail, 256, wifi->permissionBlocked
                  ? L"Windows requires Location access for Wi-Fi details."
                  : L"Windows WLAN service is unavailable.");
        return;
    }
    code = WlanEnumInterfaces(client, NULL, &interfaces);
    if (code == ERROR_ACCESS_DENIED) {
        wifi->permissionBlocked = TRUE;
        copy_wide(wifi->detail, 256, L"Allow Location services and desktop-app access to read Wi-Fi signal.");
        WlanCloseHandle(client, NULL);
        return;
    }
    if (code != ERROR_SUCCESS || !interfaces || interfaces->dwNumberOfItems == 0) {
        copy_wide(wifi->detail, 256, L"No Wi-Fi adapter was found.");
        if (interfaces) WlanFreeMemory(interfaces);
        WlanCloseHandle(client, NULL);
        return;
    }

    wifi->adapterPresent = TRUE;
    for (i = 0; i < interfaces->dwNumberOfItems; ++i) {
        PWLAN_INTERFACE_INFO info = &interfaces->InterfaceInfo[i];
        DWORD dataSize = 0;
        PWLAN_CONNECTION_ATTRIBUTES attributes = NULL;
        WLAN_OPCODE_VALUE_TYPE opcode;
        copy_wide(wifi->adapter, 128, info->strInterfaceDescription);
        code = WlanQueryInterface(client, &info->InterfaceGuid,
                                  wlan_intf_opcode_current_connection, NULL,
                                  &dataSize, (PVOID *)&attributes, &opcode);
        if (code == ERROR_ACCESS_DENIED) {
            wifi->permissionBlocked = TRUE;
            copy_wide(wifi->detail, 256, L"Allow Location services and desktop-app access to read Wi-Fi signal.");
            break;
        }
        if (code == ERROR_SUCCESS && attributes) {
            PDOT11_SSID ssid = &attributes->wlanAssociationAttributes.dot11Ssid;
            int converted;
            wifi->connected = TRUE;
            wifi->signalAvailable = TRUE;
            wifi->signal = (int)attributes->wlanAssociationAttributes.wlanSignalQuality;
            if (wifi->signal > 100) wifi->signal = 100;
            wifi->estimatedDbm = wifi->signal / 2 - 100;
            wifi->rxMbps = attributes->wlanAssociationAttributes.ulRxRate / 1000;
            wifi->txMbps = attributes->wlanAssociationAttributes.ulTxRate / 1000;
            converted = MultiByteToWideChar(CP_UTF8, 0, (const char *)ssid->ucSSID,
                                            (int)ssid->uSSIDLength, wifi->ssid,
                                            DOT11_SSID_MAX_LENGTH);
            if (converted <= 0) {
                converted = MultiByteToWideChar(CP_ACP, 0, (const char *)ssid->ucSSID,
                                                (int)ssid->uSSIDLength, wifi->ssid,
                                                DOT11_SSID_MAX_LENGTH);
            }
            if (converted > 0) wifi->ssid[converted] = L'\0';
            swprintf(wifi->detail, 256, L"Approx. %d dBm  |  Rx %lu Mbps  |  Tx %lu Mbps",
                     wifi->estimatedDbm, wifi->rxMbps, wifi->txMbps);
            WlanFreeMemory(attributes);
            break;
        }
        if (attributes) WlanFreeMemory(attributes);
    }
    if (!wifi->connected && !wifi->permissionBlocked) {
        copy_wide(wifi->detail, 256, L"Wi-Fi adapter found, but it is not connected.");
    }
    WlanFreeMemory(interfaces);
    WlanCloseHandle(client, NULL);
}

static void update_probe_stats(ProbeStats *stats, BOOL success, double latency,
                               const char *status) {
    stats->sent++;
    stats->lastSuccess = success;
    stats->lastLatency = latency;
    copy_narrow(stats->lastStatus, sizeof(stats->lastStatus), status);
    if (!success) {
        stats->failed++;
        return;
    }
    stats->totalLatency += latency;
    if (stats->sent - stats->failed == 1 || latency < stats->minLatency) stats->minLatency = latency;
    if (stats->sent - stats->failed == 1 || latency > stats->maxLatency) stats->maxLatency = latency;
}

static void write_report(BOOL stopped) {
    FILE *file;
    ProbeStats stats[PROBE_COUNT];
    WifiInfo wifi;
    char gateway[64];
    double loss;
    double latency;
    int score;
    int i;
    EnterCriticalSection(&g_state.lock);
    memcpy(stats, g_state.probes, sizeof(stats));
    wifi = g_state.wifi;
    copy_narrow(gateway, sizeof(gateway), g_state.gateway);
    LeaveCriticalSection(&g_state.lock);
    score = calculate_score(stats, &wifi, &loss, &latency);

    file = _wfopen(g_state.reportPath, L"wb");
    if (!file) return;
    fprintf(file, "Network Stability Test - Native Windows Edition\r\n");
    fprintf(file, "A Design By Bill Jiang\r\n\r\n");
    fprintf(file, "State: %s\r\n", stopped ? "Stopped early" : "Complete");
    fprintf(file, "Network rating: %d/100\r\n", score < 0 ? 0 : score);
    fprintf(file, "Packet failure: %.1f%%\r\n", loss);
    fprintf(file, "Average upstream latency: %.1f ms\r\n", latency);
    fprintf(file, "Router gateway: %s\r\n", gateway[0] ? gateway : "Not detected");
    fprintf(file, "Wi-Fi signal: %s", wifi.signalAvailable ? "available" : "unavailable");
    if (wifi.signalAvailable) fprintf(file, " (%d%%, approx. %d dBm)", wifi.signal, wifi.estimatedDbm);
    fprintf(file, "\r\nCSV log: ");
    {
        char utf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, g_state.csvPath, -1, utf8, sizeof(utf8), NULL, NULL);
        fprintf(file, "%s\r\n\r\nPer-probe summary:\r\n", utf8);
    }
    for (i = 0; i < PROBE_COUNT; ++i) {
        unsigned ok = stats[i].sent - stats[i].failed;
        double average = ok ? stats[i].totalLatency / (double)ok : 0.0;
        double failure = stats[i].sent ? (100.0 * stats[i].failed / stats[i].sent) : 0.0;
        fprintf(file, "- %s: sent=%u, ok=%u, failed=%u, failure=%.1f%%, avg=%.1f ms, last=%s\r\n",
                g_definitions[i].name, stats[i].sent, ok, stats[i].failed,
                failure, average, stats[i].lastStatus[0] ? stats[i].lastStatus : "Not tested");
    }
    fprintf(file, "\r\nQuick read:\r\n");
    if (stats[0].failed > 0) {
        fprintf(file, "Router failures point to the local Wi-Fi/Ethernet link or gateway.\r\n");
    } else {
        unsigned upstreamFailures = 0;
        for (i = 1; i < PROBE_COUNT; ++i) upstreamFailures += stats[i].failed;
        if (upstreamFailures) fprintf(file, "The local gateway is responding, but upstream checks had failures.\r\n");
        else fprintf(file, "No failures were observed in the collected samples.\r\n");
    }
    fclose(file);
}

static DWORD WINAPI probe_worker(LPVOID unused) {
    FILE *csv;
    int i;
    (void)unused;
    csv = _wfopen(g_state.csvPath, L"wb");
    if (csv) fprintf(csv, "ElapsedSeconds,Layer,Probe,Target,Success,LatencyMs,Status\r\n");

    EnterCriticalSection(&g_state.lock);
    discover_gateway(g_state.gateway, sizeof(g_state.gateway));
    LeaveCriticalSection(&g_state.lock);

    while (InterlockedCompareExchange(&g_state.stopRequested, 0, 0) == 0) {
        ULONGLONG elapsed = GetTickCount64() - g_state.startTick;
        if (elapsed >= (ULONGLONG)g_state.durationSeconds * 1000ULL) break;

        for (i = 0; i < PROBE_COUNT; ++i) {
            const ProbeDefinition *definition = &g_definitions[i];
            const char *target = definition->target;
            BOOL success = FALSE;
            double latency = 0.0;
            char status[96] = "Not run";
            char gateway[64];
            if (InterlockedCompareExchange(&g_state.stopRequested, 0, 0) != 0) break;
            if (GetTickCount64() - g_state.startTick >= (ULONGLONG)g_state.durationSeconds * 1000ULL) break;

            gateway[0] = '\0';
            EnterCriticalSection(&g_state.lock);
            copy_narrow(gateway, sizeof(gateway), g_state.gateway);
            LeaveCriticalSection(&g_state.lock);
            if (i == 0) {
                if (!gateway[0]) continue;
                target = gateway;
            }

            if (definition->kind == PROBE_PING) {
                success = ping_target(target, &latency, status, sizeof(status));
            } else if (definition->kind == PROBE_DNS) {
                success = dns_target(target, &latency, status, sizeof(status));
            } else if (i == 5) {
                success = tcp_target("1.1.1.1", "443", &latency, status, sizeof(status));
            } else {
                success = tcp_target("www.google.com", "443", &latency, status, sizeof(status));
            }

            elapsed = GetTickCount64() - g_state.startTick;
            EnterCriticalSection(&g_state.lock);
            g_state.elapsedMs = elapsed;
            update_probe_stats(&g_state.probes[i], success, latency, status);
            LeaveCriticalSection(&g_state.lock);
            if (csv) {
                fprintf(csv, "%.3f,%s,%s,%s,%s,%.1f,%s\r\n",
                        elapsed / 1000.0, definition->layer, definition->name, target,
                        success ? "true" : "false", latency, status);
                fflush(csv);
            }
            PostMessageW(g_state.hwnd, WM_APP_UPDATE, 0, 0);
        }
        for (i = 0; i < 5; ++i) {
            if (InterlockedCompareExchange(&g_state.stopRequested, 0, 0) != 0) break;
            Sleep(100);
        }
    }
    if (csv) fclose(csv);
    EnterCriticalSection(&g_state.lock);
    g_state.elapsedMs = GetTickCount64() - g_state.startTick;
    g_state.stopped = InterlockedCompareExchange(&g_state.stopRequested, 0, 0) != 0;
    g_state.running = FALSE;
    LeaveCriticalSection(&g_state.lock);
    write_report(g_state.stopped);
    PostMessageW(g_state.hwnd, WM_APP_DONE, 0, 0);
    return 0;
}

static void set_control_font(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, (WPARAM)font, TRUE);
}

static HWND make_control(HWND parent, const WCHAR *className, const WCHAR *text,
                         DWORD style, int id) {
    HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style,
                                   0, 0, 100, 24, parent, (HMENU)(INT_PTR)id,
                                   GetModuleHandleW(NULL), NULL);
    set_control_font(control, g_font);
    return control;
}

static void add_list_column(HWND list, int index, int width, const WCHAR *text) {
    LVCOLUMNW column;
    ZeroMemory(&column, sizeof(column));
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = (WCHAR *)text;
    column.cx = width;
    column.iSubItem = index;
    ListView_InsertColumn(list, index, &column);
}

static void create_controls(HWND hwnd) {
    HWND title = make_control(hwnd, L"STATIC", APP_TITLE, SS_LEFT, 0);
    HWND subtitle = make_control(hwnd, L"STATIC", L"Live Windows network and Wi-Fi diagnostics", SS_LEFT, 0);
    HWND durationLabel = make_control(hwnd, L"STATIC", L"Duration", SS_LEFT, 0);
    HWND networkBox = make_control(hwnd, L"BUTTON", L"Network rating", BS_GROUPBOX, 0);
    HWND wifiBox = make_control(hwnd, L"BUTTON", L"Wi-Fi signal", BS_GROUPBOX, 0);
    int i;
    set_control_font(title, g_titleFont);

    g_ui.duration = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"60",
                                    WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_CENTER,
                                    0, 0, 80, 28, hwnd, (HMENU)IDC_DURATION,
                                    GetModuleHandleW(NULL), NULL);
    set_control_font(g_ui.duration, g_font);
    g_ui.unit = make_control(hwnd, WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_UNIT);
    SendMessageW(g_ui.unit, CB_ADDSTRING, 0, (LPARAM)L"Minutes");
    SendMessageW(g_ui.unit, CB_ADDSTRING, 0, (LPARAM)L"Seconds");
    SendMessageW(g_ui.unit, CB_ADDSTRING, 0, (LPARAM)L"Hours");
    SendMessageW(g_ui.unit, CB_SETCURSEL, 0, 0);
    g_ui.start = make_control(hwnd, L"BUTTON", L"Start Test", BS_PUSHBUTTON | BS_DEFPUSHBUTTON, IDC_START);
    g_ui.stop = make_control(hwnd, L"BUTTON", L"Stop", BS_PUSHBUTTON, IDC_STOP);
    g_ui.reports = make_control(hwnd, L"BUTTON", L"Open Reports", BS_PUSHBUTTON, IDC_REPORTS);
    EnableWindow(g_ui.stop, FALSE);

    g_ui.progress = make_control(hwnd, PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS);
    SendMessageW(g_ui.progress, PBM_SETRANGE32, 0, 1000);
    SendMessageW(g_ui.progress, PBM_SETSTATE, PBST_NORMAL, 0);
    g_ui.remaining = make_control(hwnd, L"STATIC", L"Ready", SS_RIGHT, IDC_REMAINING);

    g_ui.netScore = make_control(hwnd, L"STATIC", L"--", SS_LEFT, IDC_NET_SCORE);
    g_ui.netGrade = make_control(hwnd, L"STATIC", L"Waiting", SS_LEFT, IDC_NET_GRADE);
    g_ui.netDetail = make_control(hwnd, L"STATIC", L"Start a test to calculate network quality.", SS_LEFT, IDC_NET_DETAIL);
    set_control_font(g_ui.netScore, g_scoreFont);

    g_ui.wifiScore = make_control(hwnd, L"STATIC", L"--%", SS_LEFT, IDC_WIFI_SCORE);
    g_ui.wifiGrade = make_control(hwnd, L"STATIC", L"Checking", SS_LEFT, IDC_WIFI_GRADE);
    g_ui.wifiDetail = make_control(hwnd, L"STATIC", L"Reading Windows wireless status.", SS_LEFT, IDC_WIFI_DETAIL);
    g_ui.location = make_control(hwnd, L"BUTTON", L"Location Settings", BS_PUSHBUTTON, IDC_LOCATION);
    ShowWindow(g_ui.location, SW_HIDE);
    set_control_font(g_ui.wifiScore, g_scoreFont);

    g_ui.probes = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                  WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                  0, 0, 100, 100, hwnd, (HMENU)IDC_PROBES,
                                  GetModuleHandleW(NULL), NULL);
    set_control_font(g_ui.probes, g_font);
    ListView_SetExtendedListViewStyle(g_ui.probes, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_GRIDLINES);
    add_list_column(g_ui.probes, 0, 115, L"Layer");
    add_list_column(g_ui.probes, 1, 210, L"Probe");
    add_list_column(g_ui.probes, 2, 80, L"Sent");
    add_list_column(g_ui.probes, 3, 80, L"Failed");
    add_list_column(g_ui.probes, 4, 100, L"Avg ms");
    add_list_column(g_ui.probes, 5, 150, L"Last status");
    for (i = 0; i < PROBE_COUNT; ++i) {
        LVITEMW item;
        WCHAR name[80];
        MultiByteToWideChar(CP_UTF8, 0, g_definitions[i].layer, -1, name, 80);
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = name;
        ListView_InsertItem(g_ui.probes, &item);
        MultiByteToWideChar(CP_UTF8, 0, g_definitions[i].name, -1, name, 80);
        ListView_SetItemText(g_ui.probes, i, 1, name);
        ListView_SetItemText(g_ui.probes, i, 2, L"0");
        ListView_SetItemText(g_ui.probes, i, 3, L"0");
        ListView_SetItemText(g_ui.probes, i, 4, L"--");
        ListView_SetItemText(g_ui.probes, i, 5, L"Not tested");
    }

    g_ui.summary = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                    L"Ready. Reports are saved automatically after each run.",
                                    WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                    0, 0, 100, 80, hwnd, (HMENU)IDC_SUMMARY,
                                    GetModuleHandleW(NULL), NULL);
    set_control_font(g_ui.summary, g_font);

    make_control(hwnd, L"STATIC", L"A Design By Bill Jiang", SS_CENTER, 900);
    SetWindowLongPtrW(title, GWLP_ID, 901);
    SetWindowLongPtrW(subtitle, GWLP_ID, 902);
    SetWindowLongPtrW(durationLabel, GWLP_ID, 903);
    SetWindowLongPtrW(networkBox, GWLP_ID, 904);
    SetWindowLongPtrW(wifiBox, GWLP_ID, 905);
}

static void layout_controls(HWND hwnd) {
    RECT rect;
    int width;
    int height;
    int margin = 20;
    int content;
    int half;
    HWND item;
    GetClientRect(hwnd, &rect);
    width = rect.right;
    height = rect.bottom;
    content = width - margin * 2;
    half = (content - 12) / 2;

    item = GetDlgItem(hwnd, 901); MoveWindow(item, margin, 15, 400, 35, TRUE);
    item = GetDlgItem(hwnd, 902); MoveWindow(item, margin, 48, 440, 24, TRUE);
    item = GetDlgItem(hwnd, 903); MoveWindow(item, margin, 86, 70, 28, TRUE);
    MoveWindow(g_ui.duration, margin + 72, 82, 70, 30, TRUE);
    MoveWindow(g_ui.unit, margin + 150, 82, 110, 200, TRUE);
    MoveWindow(g_ui.start, margin + 275, 82, 105, 30, TRUE);
    MoveWindow(g_ui.stop, margin + 388, 82, 78, 30, TRUE);
    MoveWindow(g_ui.reports, width - margin - 115, 82, 115, 30, TRUE);
    MoveWindow(g_ui.progress, margin, 123, content - 150, 15, TRUE);
    MoveWindow(g_ui.remaining, width - margin - 140, 119, 140, 22, TRUE);

    item = GetDlgItem(hwnd, 904); MoveWindow(item, margin, 151, half, 142, TRUE);
    item = GetDlgItem(hwnd, 905); MoveWindow(item, margin + half + 12, 151, half, 142, TRUE);
    MoveWindow(g_ui.netScore, margin + 18, 177, 125, 48, TRUE);
    MoveWindow(g_ui.netGrade, margin + 140, 184, half - 155, 26, TRUE);
    MoveWindow(g_ui.netDetail, margin + 18, 235, half - 36, 42, TRUE);
    MoveWindow(g_ui.wifiScore, margin + half + 30, 177, 135, 48, TRUE);
    MoveWindow(g_ui.wifiGrade, margin + half + 160, 184, half - 175, 26, TRUE);
    MoveWindow(g_ui.wifiDetail, margin + half + 30, 235, half - 180, 42, TRUE);
    MoveWindow(g_ui.location, width - margin - 144, 241, 126, 28, TRUE);

    MoveWindow(g_ui.probes, margin, 307, content, height - 307 - 148, TRUE);
    ListView_SetColumnWidth(g_ui.probes, 1, content - 115 - 80 - 80 - 100 - 160 - 5);
    MoveWindow(g_ui.summary, margin, height - 132, content, 91, TRUE);
    item = GetDlgItem(hwnd, 900); MoveWindow(item, margin, height - 30, content, 20, TRUE);
}

static void refresh_ui(void) {
    ProbeStats stats[PROBE_COUNT];
    WifiInfo wifi;
    BOOL running;
    ULONGLONG elapsed;
    int duration;
    int i;
    double loss;
    double latency;
    int score;
    WCHAR text[512];

    EnterCriticalSection(&g_state.lock);
    memcpy(stats, g_state.probes, sizeof(stats));
    wifi = g_state.wifi;
    running = g_state.running;
    elapsed = running ? GetTickCount64() - g_state.startTick : g_state.elapsedMs;
    duration = g_state.durationSeconds;
    LeaveCriticalSection(&g_state.lock);
    score = calculate_score(stats, &wifi, &loss, &latency);

    if (score < 0) copy_wide(text, 512, L"--");
    else swprintf(text, 512, L"%d", score);
    SetWindowTextW(g_ui.netScore, text);
    SetWindowTextW(g_ui.netGrade, grade_for_score(score));
    if (score < 0) {
        SetWindowTextW(g_ui.netDetail, running ? L"Collecting the first network samples." : L"Start a test to calculate network quality.");
    } else if (stats[0].failed > 0) {
        SetWindowTextW(g_ui.netDetail, L"Gateway failures indicate a local link or router problem.");
    } else if (loss > 0.0) {
        swprintf(text, 512, L"Upstream checks show %.1f%% failures. Average latency %.1f ms.", loss, latency);
        SetWindowTextW(g_ui.netDetail, text);
    } else if (latency > 150.0) {
        swprintf(text, 512, L"No loss detected, but latency is high at %.1f ms.", latency);
        SetWindowTextW(g_ui.netDetail, text);
    } else {
        swprintf(text, 512, L"No current loss. Average upstream latency %.1f ms.", latency);
        SetWindowTextW(g_ui.netDetail, text);
    }

    if (wifi.signalAvailable) {
        swprintf(text, 512, L"%d%%", wifi.signal);
        SetWindowTextW(g_ui.wifiScore, text);
        SetWindowTextW(g_ui.wifiGrade, wifi_grade(wifi.signal));
    } else {
        SetWindowTextW(g_ui.wifiScore, L"--%");
        SetWindowTextW(g_ui.wifiGrade, wifi.permissionBlocked ? L"Permission needed" :
                       wifi.connected ? L"Unavailable" : L"Not connected");
    }
    if (wifi.connected && wifi.ssid[0]) {
        swprintf(text, 512, L"%ls  |  %ls", wifi.ssid, wifi.detail);
        SetWindowTextW(g_ui.wifiDetail, text);
    } else {
        SetWindowTextW(g_ui.wifiDetail, wifi.detail);
    }
    ShowWindow(g_ui.location, wifi.permissionBlocked ? SW_SHOW : SW_HIDE);

    for (i = 0; i < PROBE_COUNT; ++i) {
        unsigned successes = stats[i].sent - stats[i].failed;
        swprintf(text, 512, L"%u", stats[i].sent);
        ListView_SetItemText(g_ui.probes, i, 2, text);
        swprintf(text, 512, L"%u", stats[i].failed);
        ListView_SetItemText(g_ui.probes, i, 3, text);
        if (successes) swprintf(text, 512, L"%.1f", stats[i].totalLatency / successes);
        else copy_wide(text, 512, L"--");
        ListView_SetItemText(g_ui.probes, i, 4, text);
        MultiByteToWideChar(CP_UTF8, 0, stats[i].lastStatus[0] ? stats[i].lastStatus : "Not tested", -1, text, 512);
        ListView_SetItemText(g_ui.probes, i, 5, text);
    }

    if (duration > 0) {
        int progress = (int)((elapsed * 1000ULL) / ((ULONGLONG)duration * 1000ULL));
        WCHAR durationText[32];
        if (progress > 1000) progress = 1000;
        SendMessageW(g_ui.progress, PBM_SETPOS, progress, 0);
        if (running) {
            ULONGLONG remaining = elapsed / 1000ULL >= (ULONGLONG)duration ? 0 :
                                  (ULONGLONG)duration - elapsed / 1000ULL;
            format_duration(durationText, 32, remaining);
            swprintf(text, 512, L"Remaining %ls", durationText);
        } else {
            format_duration(durationText, 32, elapsed / 1000ULL);
            swprintf(text, 512, L"Elapsed %ls", durationText);
        }
        SetWindowTextW(g_ui.remaining, text);
    } else {
        SetWindowTextW(g_ui.remaining, L"Ready");
    }
}

static BOOL read_duration(int *seconds) {
    WCHAR text[32];
    int value;
    int unit;
    GetWindowTextW(g_ui.duration, text, 32);
    value = _wtoi(text);
    unit = (int)SendMessageW(g_ui.unit, CB_GETCURSEL, 0, 0);
    if (value <= 0) return FALSE;
    if (unit == 0) value *= 60;
    else if (unit == 2) value *= 3600;
    if (value < 5 || value > 86400) return FALSE;
    *seconds = value;
    return TRUE;
}

static void start_test(HWND hwnd) {
    int duration;
    WifiInfo wifi;
    if (g_state.running) return;
    if (!read_duration(&duration)) {
        MessageBoxW(hwnd, L"Choose a duration from 5 seconds through 24 hours.",
                    APP_TITLE, MB_OK | MB_ICONWARNING);
        return;
    }
    if (!ensure_report_paths()) {
        MessageBoxW(hwnd, L"The report folder could not be created.", APP_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    query_wifi(&wifi);
    EnterCriticalSection(&g_state.lock);
    ZeroMemory(g_state.probes, sizeof(g_state.probes));
    ZeroMemory(g_state.gateway, sizeof(g_state.gateway));
    g_state.wifi = wifi;
    g_state.durationSeconds = duration;
    g_state.elapsedMs = 0;
    g_state.startTick = GetTickCount64();
    g_state.running = TRUE;
    g_state.stopped = FALSE;
    LeaveCriticalSection(&g_state.lock);
    InterlockedExchange(&g_state.stopRequested, 0);
    EnableWindow(g_ui.start, FALSE);
    EnableWindow(g_ui.duration, FALSE);
    EnableWindow(g_ui.unit, FALSE);
    EnableWindow(g_ui.stop, TRUE);
    SetWindowTextW(g_ui.summary, L"Test running. Stop safely at any time; partial results are preserved.");
    SendMessageW(g_ui.progress, PBM_SETPOS, 0, 0);
    g_state.worker = CreateThread(NULL, 0, probe_worker, NULL, 0, NULL);
    if (!g_state.worker) {
        EnterCriticalSection(&g_state.lock);
        g_state.running = FALSE;
        LeaveCriticalSection(&g_state.lock);
        EnableWindow(g_ui.start, TRUE);
        EnableWindow(g_ui.duration, TRUE);
        EnableWindow(g_ui.unit, TRUE);
        EnableWindow(g_ui.stop, FALSE);
        MessageBoxW(hwnd, L"The background test thread could not be started.", APP_TITLE, MB_OK | MB_ICONERROR);
    }
    refresh_ui();
}

static void request_stop(void) {
    if (!g_state.running) return;
    InterlockedExchange(&g_state.stopRequested, 1);
    EnableWindow(g_ui.stop, FALSE);
    SetWindowTextW(g_ui.summary, L"Stopping after the active probe. Finalizing the partial report...");
}

static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            g_state.hwnd = hwnd;
            create_controls(hwnd);
            query_wifi(&g_state.wifi);
            SetTimer(hwnd, 1, 1000, NULL);
            refresh_ui();
            return 0;
        case WM_SIZE:
            layout_controls(hwnd);
            return 0;
        case WM_GETMINMAXINFO: {
            MINMAXINFO *info = (MINMAXINFO *)lParam;
            info->ptMinTrackSize.x = 820;
            info->ptMinTrackSize.y = 650;
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                static int wifiTicks = 0;
                if (++wifiTicks >= 2) {
                    WifiInfo wifi;
                    query_wifi(&wifi);
                    EnterCriticalSection(&g_state.lock);
                    g_state.wifi = wifi;
                    LeaveCriticalSection(&g_state.lock);
                    wifiTicks = 0;
                }
                refresh_ui();
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_START: start_test(hwnd); return 0;
                case IDC_STOP: request_stop(); return 0;
                case IDC_REPORTS:
                    if (ensure_report_directory()) {
                        ShellExecuteW(hwnd, L"open", g_state.reportDir, NULL, NULL, SW_SHOWNORMAL);
                    } else {
                        MessageBoxW(hwnd, L"The reports folder could not be opened.", APP_TITLE,
                                    MB_OK | MB_ICONERROR);
                    }
                    return 0;
                case IDC_LOCATION:
                    ShellExecuteW(hwnd, L"open", L"ms-settings:privacy-location", NULL, NULL, SW_SHOWNORMAL);
                    return 0;
            }
            break;
        case WM_APP_UPDATE:
            refresh_ui();
            return 0;
        case WM_APP_DONE: {
            WCHAR messageText[MAX_PATH + 160];
            if (g_state.worker) {
                CloseHandle(g_state.worker);
                g_state.worker = NULL;
            }
            refresh_ui();
            EnableWindow(g_ui.start, TRUE);
            EnableWindow(g_ui.duration, TRUE);
            EnableWindow(g_ui.unit, TRUE);
            EnableWindow(g_ui.stop, FALSE);
            swprintf(messageText, MAX_PATH + 160,
                     g_state.stopped ? L"Test stopped safely. Partial report saved:\r\n%ls"
                                     : L"Test complete. Report saved:\r\n%ls",
                     g_state.reportPath);
            SetWindowTextW(g_ui.summary, messageText);
            if (g_state.closing) DestroyWindow(hwnd);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = (HDC)wParam;
            SetBkColor(dc, RGB(247, 249, 250));
            SetTextColor(dc, RGB(24, 32, 38));
            return (LRESULT)g_background;
        }
        case WM_CLOSE:
            if (g_state.running) {
                int answer = MessageBoxW(hwnd, L"Stop the active test and close the application?\nThe partial report will be saved.",
                                         APP_TITLE, MB_YESNO | MB_ICONQUESTION);
                if (answer != IDYES) return 0;
                g_state.closing = TRUE;
                request_stop();
                EnableWindow(hwnd, FALSE);
                return 0;
            }
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand) {
    WNDCLASSEXW windowClass;
    INITCOMMONCONTROLSEX controls;
    WSADATA winsock;
    HWND hwnd;
    MSG message;
    (void)previous;
    (void)commandLine;

    SetProcessDPIAware();
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
        MessageBoxW(NULL, L"Windows networking could not be initialized.", APP_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    ZeroMemory(&g_state, sizeof(g_state));
    InitializeCriticalSection(&g_state.lock);
    g_background = CreateSolidBrush(RGB(247, 249, 250));
    g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_titleFont = CreateFontW(-27, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_scoreFont = CreateFontW(-38, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = window_proc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    windowClass.hbrBackground = g_background;
    windowClass.lpszClassName = L"NetworkStabilityTestNativeWindow";
    if (!RegisterClassExW(&windowClass)) return 1;

    hwnd = CreateWindowExW(0, windowClass.lpszClassName, APP_TITLE,
                           WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                           CW_USEDEFAULT, CW_USEDEFAULT, 1080, 760,
                           NULL, NULL, instance, NULL);
    if (!hwnd) return 1;
    ShowWindow(hwnd, showCommand);
    UpdateWindow(hwnd);

    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    DeleteObject(g_font);
    DeleteObject(g_titleFont);
    DeleteObject(g_scoreFont);
    DeleteObject(g_background);
    DeleteCriticalSection(&g_state.lock);
    WSACleanup();
    return (int)message.wParam;
}
