/******************************************************************************
 **                                                                          **
 ** Santa Paravia & Fiumaccio. Translated from the original TRS-80 BASIC    **
 ** source code into C by Thomas Knox <tknox@mac.com>.                      **
 **                                                                          **
 ** Original program (C) 1979 by George Blank                               **
 ** <gwblank@postoffice.worldnet.att.net>                                   **
 **                                                                          **
 ** Curses TUI, bug fixes, and modernisation by Tom Knox, 2000/2026.        **
 **                                                                          **
 ******************************************************************************/

/*
Copyright (C) 2000 Thomas Knox
Portions Copyright (C) 1979 by George Blank, used with permission.
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ncurses.h>

/* ---------------------------------------------------------------------------
 * Layout constants — all positions derived from LINES/COLS at runtime.
 * We divide the screen into:
 *   Left  panel: map           (MAP_COLS wide)
 *   Right panel: stats         (COLS - MAP_COLS wide)
 *   Bottom strip: message log  (MSG_LINES tall)
 * --------------------------------------------------------------------------*/
#define MIN_COLS        80
#define MIN_LINES       24
#define MSG_LINES        8      /* lines reserved for the message area        */
#define MAP_COLS_FRAC    0.55   /* map takes ~55% of screen width             */
#define LOG_ROWS  (MSG_LINES - 4)   /* visible log lines (excl borders+divider+input) */
#define LOG_MAX   64                /* ring buffer capacity                        */

/* Color pair IDs */
#define CP_NORMAL        1
#define CP_WALL          2
#define CP_TOWER         3
#define CP_FIELDS        4
#define CP_BUILDINGS     5
#define CP_STATS_HDR     6
#define CP_STATS_VAL     7
#define CP_MSG           8
#define CP_TITLE         9
#define CP_WARNING      10

/* ---------------------------------------------------------------------------
 * Player struct
 * --------------------------------------------------------------------------*/
struct Player {
    int  Cathedral, Clergy, CustomsDuty, CustomsDutyRevenue, DeadSerfs;
    int  Difficulty, FleeingSerfs, GrainDemand, GrainPrice, GrainReserve;
    int  Harvest, IncomeTax, IncomeTaxRevenue, RatsAte;
    int  Justice, JusticeRevenue, Land, Marketplaces, MarketRevenue;
    int  Merchants, MillRevenue, Mills, NewSerfs, Nobles, OldTitle, Palace;
    int  Rats, SalesTax, SalesTaxRevenue, Serfs, SoldierPay, Soldiers, TitleNum;
    int  TransplantedSerfs, Treasury, WhichPlayer, Year, YearOfDeath;
    char City[15], Name[25], Title[15];
    float PublicWorks, LandPrice;
    bool InvadeMe, IsBankrupt, IsDead, IWon, MaleOrFemale, NewTitle;
};
typedef struct Player player;
#ifdef NETWORK_MODE
#include <curl/curl.h>
#include <pthread.h>

/* Forward declarations of TUI and game functions used by network code */
void    MsgPrint(const char *fmt, ...);
void    MsgClear(void);
int     MsgGetInt(const char *prompt, int lo, int hi);
void    MsgGetStr(const char *prompt, char *buf, int maxlen);
int     MsgGetGrain(int minimum, int maximum);
void    MsgWaitEnter(void);
void    DrawMap(player *Me);
void    DrawStats(player *Me);
void    PrintGrain(player *Me);
void    ShowStats(player players[], int howMany);

/* ---------------------------------------------------------------------------
 * Network configuration — set by command line args
 * --------------------------------------------------------------------------*/
static char  g_server_url[256]   = "http://localhost:8765";
static char  g_game_id[32]       = "";
static char  g_player_token[64]  = "";
static int   g_my_player_index   = -1;
static bool  g_my_turn           = false;
static int   g_latest_seq        = 0;

/* ---------------------------------------------------------------------------
 * Thread-safe event queue
 * --------------------------------------------------------------------------*/
#define NET_QUEUE_MAX 256

typedef struct {
    int   seq;
    char  type[32];
    char  message[512];
    int   player_index;
} net_event_t;

static net_event_t      g_net_queue[NET_QUEUE_MAX];
static int              g_net_queue_head  = 0;
static int              g_net_queue_tail  = 0;
static pthread_mutex_t  g_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t   g_queue_cond      = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t  g_state_mutex     = PTHREAD_MUTEX_INITIALIZER;
static bool             g_net_stop        = false;
static bool             g_game_over_net   = false;

static void net_queue_push(int seq, const char *type,
                           const char *message, int pidx)
{
    pthread_mutex_lock(&g_queue_mutex);
    int next = (g_net_queue_tail + 1) % NET_QUEUE_MAX;
    if (next != g_net_queue_head) {
        g_net_queue[g_net_queue_tail].seq          = seq;
        g_net_queue[g_net_queue_tail].player_index = pidx;
        strncpy(g_net_queue[g_net_queue_tail].type,    type,    31);
        strncpy(g_net_queue[g_net_queue_tail].message, message, 511);
        g_net_queue[g_net_queue_tail].type[31]     = '\0';
        g_net_queue[g_net_queue_tail].message[511] = '\0';
        g_net_queue_tail = next;
    }
    pthread_cond_signal(&g_queue_cond);
    pthread_mutex_unlock(&g_queue_mutex);
}

static bool net_queue_pop(net_event_t *out)
{
    pthread_mutex_lock(&g_queue_mutex);
    if (g_net_queue_head == g_net_queue_tail) {
        pthread_mutex_unlock(&g_queue_mutex);
        return false;
    }
    *out = g_net_queue[g_net_queue_head];
    g_net_queue_head = (g_net_queue_head + 1) % NET_QUEUE_MAX;
    pthread_mutex_unlock(&g_queue_mutex);
    return true;
}

/* ---------------------------------------------------------------------------
 * libcurl helpers
 * --------------------------------------------------------------------------*/
typedef struct { char *buf; size_t len; size_t cap; } curl_buf_t;

static size_t curl_write_cb(void *data, size_t size, size_t nmemb, void *userp)
{
    curl_buf_t *b = (curl_buf_t *)userp;
    size_t total  = size * nmemb;
    size_t needed = b->len + total + 1;
    if (needed > b->cap) {
        b->cap = needed * 2;
        b->buf = realloc(b->buf, b->cap);
        if (!b->buf) return 0;
    }
    memcpy(b->buf + b->len, data, total);
    b->len += total;
    b->buf[b->len] = '\0';
    return total;
}

static char *net_get(const char *url)
{
    CURL       *curl = curl_easy_init();
    curl_buf_t  body = {0};
    if (!curl) return NULL;
    body.buf = malloc(256); body.cap = 256; body.len = 0;
    curl_easy_setopt(curl, CURLOPT_URL,          url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,    &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       12L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(body.buf); return NULL; }
    return body.buf;
}

static char *net_post(const char *url, const char *json_body,
                      const char *token)
{
    CURL              *curl = curl_easy_init();
    curl_buf_t         body = {0};
    struct curl_slist *hdrs = NULL;
    if (!curl) return NULL;
    body.buf = malloc(256); body.cap = 256; body.len = 0;
    hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
    if (token && token[0]) {
        char auth[128];
        snprintf(auth, sizeof(auth), "x-player-token: %s", token);
        hdrs = curl_slist_append(hdrs, auth);
    }
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,    hdrs);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS,    json_body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,     &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        10L);
    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) { free(body.buf); return NULL; }
    return body.buf;
}

/* ---------------------------------------------------------------------------
 * Minimal JSON field extractors
 * --------------------------------------------------------------------------*/
static bool json_str(const char *json, const char *key,
                     char *out, int maxlen)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p != '"') return false;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < maxlen - 1) out[i++] = *p++;
    out[i] = '\0';
    return true;
}

static bool json_int(const char *json, const char *key, int *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) { *out = atoi(p); return true; }
    return false;
}

static bool json_bool(const char *json, const char *key, bool *out)
{
    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return false;
    p += strlen(needle);
    while (*p == ' ' || *p == ':') p++;
    if (strncmp(p, "true",  4) == 0) { *out = true;  return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

/* ---------------------------------------------------------------------------
 * Poll thread
 * --------------------------------------------------------------------------*/
static void *poll_thread_fn(void *arg)
{
    (void)arg;
    char url[512];
    while (!g_net_stop) {
        snprintf(url, sizeof(url), "%s/game/%s/log?since=%d&timeout=10",
                 g_server_url, g_game_id, g_latest_seq);
        char *resp = net_get(url);
        if (!resp) {
            struct timespec ts = {1, 0};
            nanosleep(&ts, NULL);
            continue;
        }
        const char *p = resp;
        while ((p = strstr(p, "{\"seq\"")) != NULL) {
            int  seq = 0, pidx = -1;
            char type[32] = "", msg[512] = "";
            const char *obj_start = p;
            int depth = 0;
            const char *obj_end = p;
            while (*obj_end) {
                if (*obj_end == '{') depth++;
                if (*obj_end == '}') { depth--; if (!depth) break; }
                obj_end++;
            }
            size_t obj_len = (size_t)(obj_end - obj_start + 1);
            char *obj = malloc(obj_len + 1);
            if (!obj) { p = obj_end + 1; continue; }
            memcpy(obj, obj_start, obj_len);
            obj[obj_len] = '\0';
            json_int(obj, "seq",          &seq);
            json_int(obj, "player_index", &pidx);
            json_str(obj, "type",          type, sizeof(type));
            json_str(obj, "message",       msg,  sizeof(msg));
            free(obj);
            if (seq > g_latest_seq) {
                g_latest_seq = seq;
                if (strcmp(type, "turn_start") == 0) {
                    pthread_mutex_lock(&g_state_mutex);
                    g_my_turn = (pidx == g_my_player_index);
                    pthread_mutex_unlock(&g_state_mutex);
                }
                if (strcmp(type, "game_over") == 0)
                    g_game_over_net = true;
                net_queue_push(seq, type, msg, pidx);
            }
            p = obj_end + 1;
        }
        free(resp);
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Apply server state delta to local player struct
 * --------------------------------------------------------------------------*/
static void apply_player_state(player *Me, const char *json)
{
    int  ival;
    bool bval;
    char sval[32];
    pthread_mutex_lock(&g_state_mutex);
#define AI(f,k) if (json_int(json,  k, &ival)) Me->f = ival
#define AS(f,k,n) if (json_str(json, k, sval, sizeof(sval))) \
                      strncpy(Me->f, sval, (n)-1)
#define AB(f,k) if (json_bool(json, k, &bval)) Me->f = bval
    AI(Treasury,     "treasury");
    AI(Land,         "land");
    AI(GrainReserve, "grain_reserve");
    AI(GrainDemand,  "grain_demand");
    AI(GrainPrice,   "grain_price");
    AI(Serfs,        "serfs");
    AI(Soldiers,     "soldiers");
    AI(Nobles,       "nobles");
    AI(Clergy,       "clergy");
    AI(Merchants,    "merchants");
    AI(Cathedral,    "cathedral");
    AI(Palace,       "palace");
    AI(Marketplaces, "marketplaces");
    AI(Mills,        "mills");
    AI(CustomsDuty,  "customs_duty");
    AI(SalesTax,     "sales_tax");
    AI(IncomeTax,    "income_tax");
    AI(Justice,      "justice");
    AI(TitleNum,     "title_num");
    AI(Year,         "year");
    AI(Harvest,      "harvest");
    AI(Rats,         "rats");
    AI(RatsAte,      "rats_ate");
    AS(Title,        "title", 15);
    AS(Name,         "name",  25);
    AB(IsDead,       "is_dead");
    AB(IsBankrupt,   "is_bankrupt");
    AB(IWon,         "i_won");
    AB(InvadeMe,     "invade_me");
    const char *fp;
    fp = strstr(json, "\"land_price\"");
    if (fp) { fp = strchr(fp, ':'); if (fp) Me->LandPrice = (float)atof(fp+1); }
    fp = strstr(json, "\"public_works\"");
    if (fp) { fp = strchr(fp, ':'); if (fp) Me->PublicWorks = (float)atof(fp+1); }
#undef AI
#undef AS
#undef AB
    pthread_mutex_unlock(&g_state_mutex);
}

/* Fetch full state and apply one player's data */
static void net_refresh_player(player *Me)
{
    char url[512];
    snprintf(url, sizeof(url), "%s/game/%s/state", g_server_url, g_game_id);
    char *resp = net_get(url);
    if (!resp) return;
    /* Find this player's object in the players array */
    char needle[32];
    snprintf(needle, sizeof(needle), "\"which_player\":%d", Me->WhichPlayer);
    const char *pp = strstr(resp, needle);
    if (pp) {
        while (pp > resp && *pp != '{') pp--;
        apply_player_state(Me, pp);
    }
    free(resp);
}

/* ---------------------------------------------------------------------------
 * Network action wrappers
 * --------------------------------------------------------------------------*/
static char *net_action(const char *type, const char *params_json)
{
    char url[512], body[1024];
    snprintf(url,  sizeof(url),  "%s/game/%s/action", g_server_url, g_game_id);
    snprintf(body, sizeof(body), "{\"type\":\"%s\",\"params\":{%s}}",
             type, params_json);
    return net_post(url, body, g_player_token);
}

static void net_buy_grain(player *Me)
{
    int howmuch = MsgGetInt("Buy grain (0=specify total)? ", 0, 999999);
    if (howmuch == 0) {
        int total = MsgGetInt("Total grain desired? ", Me->GrainReserve, 999999);
        howmuch = total - Me->GrainReserve;
    }
    char p[64]; snprintf(p, sizeof(p), "\"amount\":%d", howmuch);
    char *r = net_action("buy_grain", p); if (r) free(r);
}

static void net_sell_grain(player *Me)
{
    int howmuch = MsgGetInt("Sell how much grain? ", 0, Me->GrainReserve);
    char p[64]; snprintf(p, sizeof(p), "\"amount\":%d", howmuch);
    char *r = net_action("sell_grain", p); if (r) free(r);
}

static void net_buy_land(player *UNUSED)
{
    (void)UNUSED;
    int howmuch = MsgGetInt("Buy how many hectares? ", 0, 999999);
    char p[64]; snprintf(p, sizeof(p), "\"amount\":%d", howmuch);
    char *r = net_action("buy_land", p); if (r) free(r);
}

static void net_sell_land(player *Me)
{
    int maxsell = Me->Land - 5000;
    if (maxsell <= 0) { MsgPrint("No land to sell."); return; }
    int howmuch = MsgGetInt("Sell how many hectares? ", 0, maxsell);
    char p[64]; snprintf(p, sizeof(p), "\"amount\":%d", howmuch);
    char *r = net_action("sell_land", p); if (r) free(r);
}

static void net_release_grain(player *Me)
{
    int minimum = Me->GrainReserve / 5;
    int maximum = Me->GrainReserve - minimum;
    MsgPrint("Grain demand: %d steres.", Me->GrainDemand);
    int howmuch = MsgGetGrain(minimum, maximum);
    char p[64]; snprintf(p, sizeof(p), "\"amount\":%d", howmuch);
    char *r = net_action("release_grain", p); if (r) free(r);
}

static void net_set_tax(const char *tax_type, int value)
{
    char p[128];
    snprintf(p, sizeof(p), "\"tax_type\":\"%s\",\"value\":%d", tax_type, value);
    char *r = net_action("set_tax", p); if (r) free(r);
}

static void net_buy_building(const char *building)
{
    char p[64]; snprintf(p, sizeof(p), "\"building\":\"%s\"", building);
    char *r = net_action("buy_building", p); if (r) free(r);
}

/* ---------------------------------------------------------------------------
 * Network game phases
 * --------------------------------------------------------------------------*/
static void net_buy_sell_grain(player *Me, player players[], int n)
{
    char buf[4];
    while (true) {
        MsgClear();
        MsgPrint("Year %d — %s %s   Rats ate %d%% of grain.",
                 Me->Year, Me->Title, Me->Name, Me->Rats);
        PrintGrain(Me);
        DrawMap(Me);
        DrawStats(Me);
        MsgPrint("1=Buy grain  2=Sell grain  3=Buy land  4=Sell land  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));
        char ch = buf[0];
        if (ch == 'q' || ch == 'Q') break;
        if (ch == '1') net_buy_grain(Me);
        if (ch == '2') net_sell_grain(Me);
        if (ch == '3') net_buy_land(Me);
        if (ch == '4') net_sell_land(Me);
        net_refresh_player(Me);
        DrawMap(Me); DrawStats(Me);
    }
    (void)players; (void)n;
}

static void net_adjust_tax(player *Me)
{
    char buf[4]; int duty;
    while (true) {
        MsgClear(); DrawStats(Me);
        MsgPrint("Customs:%d%%  Sales:%d%%  Wealth:%d%%",
                 Me->CustomsDuty, Me->SalesTax, Me->IncomeTax);
        MsgPrint("1=Customs  2=Sales  3=Wealth  4=Justice  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        switch (atoi(buf)) {
            case 1: duty=MsgGetInt("Customs (0-100): ",0,100);
                    net_set_tax("customs", duty); break;
            case 2: duty=MsgGetInt("Sales tax (0-50): ",0,50);
                    net_set_tax("sales",   duty); break;
            case 3: duty=MsgGetInt("Wealth tax (0-25): ",0,25);
                    net_set_tax("income",  duty); break;
            case 4: duty=MsgGetInt("Justice 1-4: ",1,4);
                    net_set_tax("justice", duty); break;
        }
        net_refresh_player(Me); DrawStats(Me);
    }
    char *r = net_action("end_tax_phase", ""); if (r) free(r);
    net_refresh_player(Me);
}

static void net_state_purchases(player *Me, player players[], int n)
{
    char buf[4];
    while (true) {
        MsgClear();
        MsgPrint("%s %s — Treasury: %d fl", Me->Title, Me->Name, Me->Treasury);
        MsgPrint("1=Market  2=Mill  3=Palace  4=Cathedral  5=Soldiers"
                 "  6=Standings  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));
        if (buf[0] == 'q' || buf[0] == 'Q') break;
        switch (atoi(buf)) {
            case 1: net_buy_building("market");    break;
            case 2: net_buy_building("mill");      break;
            case 3: net_buy_building("palace");    break;
            case 4: net_buy_building("cathedral"); break;
            case 5: net_buy_building("soldiers");  break;
            case 6: ShowStats(players, n);         break;
        }
        net_refresh_player(Me);
        DrawMap(Me); DrawStats(Me);
    }
    char *r = net_action("end_turn", ""); if (r) free(r);
}

/* ---------------------------------------------------------------------------
 * Watcher loop — runs while it is not our turn
 * --------------------------------------------------------------------------*/
static void net_watch_loop(player players[], int n, int my_index)
{
    net_event_t ev;
    MsgPrint("Watching... waiting for your turn.");
    while (!g_my_turn && !g_game_over_net) {
        bool had = false;
        while (net_queue_pop(&ev)) {
            MsgPrint("%s", ev.message);
            if (ev.player_index >= 0 && ev.player_index < n)
                net_refresh_player(&players[ev.player_index]);
            DrawMap(&players[ev.player_index >= 0 &&
                              ev.player_index < n ?
                              ev.player_index : my_index]);
            DrawStats(&players[my_index]);
            had = true;
        }
        if (!had) { struct timespec ts={0,50000000L}; nanosleep(&ts,NULL); }
    }
    (void)n;
}

/* ---------------------------------------------------------------------------
 * Net turn coordinator
 * --------------------------------------------------------------------------*/
static void net_new_turn(player players[], int n, int my_index)
{
    net_event_t ev;
    while (net_queue_pop(&ev)) MsgPrint("%s", ev.message);

    player *Me = &players[my_index];
    if (g_my_turn) {
        net_refresh_player(Me);
        DrawMap(Me); DrawStats(Me);
        net_buy_sell_grain(Me, players, n);
        net_release_grain(Me);
        net_refresh_player(Me);
        net_adjust_tax(Me);
        DrawMap(Me); DrawStats(Me);
        net_state_purchases(Me, players, n);
    } else {
        net_watch_loop(players, n, my_index);
    }
}

/* ---------------------------------------------------------------------------
 * Net PlayGame
 * --------------------------------------------------------------------------*/
static void net_play_game(player players[], int n, int my_index)
{
    pthread_t poll_tid;
    pthread_create(&poll_tid, NULL, poll_thread_fn, NULL);

    char url[512];
    snprintf(url, sizeof(url), "%s/game/%s/ready", g_server_url, g_game_id);
    char *resp = net_post(url, "{}", g_player_token); if (resp) free(resp);

    MsgPrint("Waiting for all players to be ready...");

    /* Wait for the first turn_start event */
    net_event_t ev;
    for (;;) {
        if (net_queue_pop(&ev)) {
            MsgPrint("%s", ev.message);
            if (strcmp(ev.type, "turn_start") == 0 ||
                strcmp(ev.type, "game_over")  == 0) break;
        } else {
            struct timespec ts = {0, 50000000L};
            nanosleep(&ts, NULL);
        }
    }

    while (!g_game_over_net)
        net_new_turn(players, n, my_index);

    g_net_stop = true;
    pthread_join(poll_tid, NULL);
    MsgClear();
    MsgPrint("Game over. Thanks for playing!");
    MsgWaitEnter();
}

#endif /* NETWORK_MODE */


/* ---------------------------------------------------------------------------
 * Global curses windows
 * --------------------------------------------------------------------------*/
static WINDOW *w_map   = NULL;   /* left panel: the city map          */
static WINDOW *w_stats = NULL;   /* right panel: player statistics    */
static WINDOW *w_msg   = NULL;   /* bottom strip: message log         */
static bool    g_color = false;  /* whether color is available        */

/* Cached terminal dimensions (set in InitWindows, refreshed on resize) */
static int g_lines = 0;
static int g_cols  = 0;
static int g_map_cols   = 0;
static int g_map_lines  = 0;
static int g_stat_cols  = 0;

/* ---------------------------------------------------------------------------
 * City / title tables
 * --------------------------------------------------------------------------*/
static const char CityList[7][15] = {
    "Santa Paravia", "Fiumaccio",  "Torricella",
    "Molinetto",     "Fontanile",  "Romanga",    "Monterana"
};

static const char MaleTitles[8][15] = {
    "Sir", "Baron", "Count", "Marquis",
    "Duke", "Grand Duke", "Prince", "* H.R.H. King"
};

static const char FemaleTitles[8][15] = {
    "Lady", "Baroness", "Countess", "Marquise",
    "Duchess", "Grand Duchess", "Princess", "* H.R.H. Queen"
};

/* ---------------------------------------------------------------------------
 * Prototypes
 * --------------------------------------------------------------------------*/
/* TUI management */
void  InitWindows(void);
void  TeardownWindows(void);
void  ResizeWindows(void);
void  MsgPrint(const char *fmt, ...);
void  MsgClear(void);
int   MsgGetInt(const char *prompt, int lo, int hi);
void  MsgGetStr(const char *prompt, char *buf, int maxlen);
int   MsgGetGrain(int minimum, int maximum);
void  MsgWaitEnter(void);
void  DrawMap(player *Me);
void  DrawStats(player *Me);
void  SetColor(WINDOW *w, int pair, bool on);

/* Game logic */
int     main(int argc, char *argv[]);
int     Random(int hi);
void    InitializePlayer(player *Me, int year, int city, int level,
                         const char *name, bool MorF);
void    AddRevenue(player *Me);
int     AttackNeighbor(player *Me, player *Him);
void    BuyCathedral(player *Me);
void    BuyGrain(player *Me);
void    BuyLand(player *Me);
void    BuyMarket(player *Me);
void    BuyMill(player *Me);
void    BuyPalace(player *Me);
void    BuySoldiers(player *Me);
int     limit10(int num, int denom);
bool    CheckNewTitle(player *Me);
void    GenerateHarvest(player *Me);
void    GenerateIncome(player *Me);
void    ChangeTitle(player *Me);
void    NewLandAndGrainPrices(player *Me);
void    PrintGrain(player *Me);
int     ReleaseGrain(player *Me);
void    SeizeAssets(player *Me);
void    SellGrain(player *Me);
void    SellLand(player *Me);
void    SerfsDecomposing(player *Me, float scale);
void    SerfsProcreating(player *Me, float scale);
void    PrintInstructions(void);
void    PlayGame(player players[], int n);
void    NewTurn(player *Me, int howMany, player players[], player *Baron);
void    BuySellGrain(player *Me);
void    AdjustTax(player *Me);
void    StatePurchases(player *Me, int howMany, player players[]);
void    ShowStats(player players[], int howMany);
void    ImDead(player *Me);

/* ===========================================================================
 * TUI — window management
 * =========================================================================*/

void InitWindows(void)
{
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    g_color = has_colors();
    if (g_color) {
        start_color();
        use_default_colors();
        init_pair(CP_NORMAL,    COLOR_WHITE,   -1);
        init_pair(CP_WALL,      COLOR_YELLOW,  -1);
        init_pair(CP_TOWER,     COLOR_CYAN,    -1);
        init_pair(CP_FIELDS,    COLOR_GREEN,   -1);
        init_pair(CP_BUILDINGS, COLOR_MAGENTA, -1);
        init_pair(CP_STATS_HDR, COLOR_CYAN,    -1);
        init_pair(CP_STATS_VAL, COLOR_WHITE,   -1);
        init_pair(CP_MSG,       COLOR_WHITE,   -1);
        init_pair(CP_TITLE,     COLOR_YELLOW,  -1);
        init_pair(CP_WARNING,   COLOR_RED,     -1);
    }

    ResizeWindows();
}

void ResizeWindows(void)
{
    getmaxyx(stdscr, g_lines, g_cols);

    g_map_cols  = (int)(g_cols * MAP_COLS_FRAC);
    g_map_lines = g_lines - MSG_LINES;
    g_stat_cols = g_cols - g_map_cols;

    if (w_map)   { delwin(w_map);   w_map   = NULL; }
    if (w_stats) { delwin(w_stats); w_stats = NULL; }
    if (w_msg)   { delwin(w_msg);   w_msg   = NULL; }

    w_map   = newwin(g_map_lines, g_map_cols,  0,            0);
    w_stats = newwin(g_map_lines, g_stat_cols, 0,            g_map_cols);
    w_msg   = newwin(MSG_LINES,   g_cols,      g_map_lines,  0);

    box(w_map,   0, 0);
    box(w_stats, 0, 0);
    box(w_msg,   0, 0);

    wrefresh(w_map);
    wrefresh(w_stats);
    wrefresh(w_msg);
}

void TeardownWindows(void)
{
    if (w_map)   delwin(w_map);
    if (w_stats) delwin(w_stats);
    if (w_msg)   delwin(w_msg);
    endwin();
}

void SetColor(WINDOW *w, int pair, bool on)
{
    if (!g_color) return;
    if (on)
        wattron(w,  COLOR_PAIR(pair));
    else
        wattroff(w, COLOR_PAIR(pair));
}

/* ---------------------------------------------------------------------------
 * Message window helpers
 *
 * Layout inside w_msg (MSG_LINES rows tall, g_cols wide):
 *
 *   row 0              : box top border
 *   rows 1..LOG_ROWS   : scrolling log lines  (LOG_ROWS = MSG_LINES - 4)
 *   row MSG_LINES-3    : box-interior divider (ACS_HLINE)
 *   row MSG_LINES-2    : dedicated input/prompt line  <-- NEVER scrolled
 *   row MSG_LINES-1    : box bottom border
 *
 * We manage scrolling ourselves with a simple ring buffer of strings so
 * that wscrl() — which fights with box borders — is never called.
 * --------------------------------------------------------------------------*/

#define LOG_ROWS  (MSG_LINES - 4)   /* number of visible log lines          */
#define LOG_MAX   64                /* ring buffer capacity                 */

static char  g_log[LOG_MAX][512];  /* ring buffer of log strings           */
static int   g_log_head = 0;       /* index of oldest entry                */
static int   g_log_count = 0;      /* number of valid entries              */

/* Repaint the entire message window from the ring buffer. */
static void MsgRepaint(void)
{
    int r, entry;

    werase(w_msg);
    box(w_msg, 0, 0);

    /* Divider above the input line */
    mvwhline(w_msg, MSG_LINES - 3, 1, ACS_HLINE, g_cols - 2);

    /* Paint log lines, most-recent at bottom */
    for (r = 0; r < LOG_ROWS; r++) {
        int age = LOG_ROWS - 1 - r;          /* 0 = most recent            */
        if (age >= g_log_count) continue;    /* no entry that old yet      */
        entry = (g_log_head + g_log_count - 1 - age) % LOG_MAX;
        SetColor(w_msg, CP_MSG, true);
        mvwaddnstr(w_msg, r + 1, 1, g_log[entry], g_cols - 2);
        SetColor(w_msg, CP_MSG, false);
    }

    wrefresh(w_msg);
}

void MsgClear(void)
{
    g_log_head  = 0;
    g_log_count = 0;
    MsgRepaint();
}

/* Append a line to the log ring buffer and repaint. */
void MsgPrint(const char *fmt, ...)
{
    va_list ap;
    int     slot;

    va_start(ap, fmt);
    if (g_log_count < LOG_MAX) {
        slot = (g_log_head + g_log_count) % LOG_MAX;
        g_log_count++;
    } else {
        /* Buffer full: overwrite oldest entry and advance head */
        slot      = g_log_head;
        g_log_head = (g_log_head + 1) % LOG_MAX;
    }
    vsnprintf(g_log[slot], sizeof(g_log[slot]), fmt, ap);
    va_end(ap);

    MsgRepaint();
}

/* Write prompt to the dedicated input line, read and validate an integer. */
int MsgGetInt(const char *prompt, int lo, int hi)
{
    char buf[64];
    int  val;

    while (true) {
        /* Clear and write prompt on the input line */
        wmove(w_msg, MSG_LINES - 2, 1);
        wclrtoeol(w_msg);
        /* Restore right border erased by clrtoeol */
        mvwaddch(w_msg, MSG_LINES - 2, g_cols - 1, ACS_VLINE);
        SetColor(w_msg, CP_MSG, true);
        mvwaddnstr(w_msg, MSG_LINES - 2, 1, prompt, g_cols - 4);
        SetColor(w_msg, CP_MSG, false);

        /* Position cursor right after the prompt text */
        int prompt_len = (int)strlen(prompt);
        if (prompt_len > g_cols - 4) prompt_len = g_cols - 4;
        wmove(w_msg, MSG_LINES - 2, 1 + prompt_len);
        wrefresh(w_msg);

        echo();
        curs_set(1);
        wgetnstr(w_msg, buf, (int)sizeof(buf) - 1);
        curs_set(0);
        noecho();

        val = atoi(buf);
        if (val >= lo && val <= hi)
            return val;

        MsgPrint("Enter a value between %d and %d.", lo, hi);
    }
}

/* Write prompt to the input line, read a raw string. */
void MsgGetStr(const char *prompt, char *out, int maxlen)
{
    wmove(w_msg, MSG_LINES - 2, 1);
    wclrtoeol(w_msg);
    mvwaddch(w_msg, MSG_LINES - 2, g_cols - 1, ACS_VLINE);
    SetColor(w_msg, CP_MSG, true);
    mvwaddnstr(w_msg, MSG_LINES - 2, 1, prompt, g_cols - 4);
    SetColor(w_msg, CP_MSG, false);

    int prompt_len = (int)strlen(prompt);
    if (prompt_len > g_cols - 4) prompt_len = g_cols - 4;
    wmove(w_msg, MSG_LINES - 2, 1 + prompt_len);
    wrefresh(w_msg);

    echo();
    curs_set(1);
    wgetnstr(w_msg, out, maxlen - 1);
    curs_set(0);
    noecho();
    out[maxlen - 1] = '\0';
}

/* Grain-release prompt: accepts 1=min, 2=max, or a typed value.
 * Shows min/max inline so the player can see them without scrolling up. */
int MsgGetGrain(int minimum, int maximum)
{
    char buf[64];
    int  val;

    while (true) {
        /* Build prompt with live min/max values */
        char prompt[128];
        snprintf(prompt, sizeof(prompt),
                 "Release grain  1=Min(%d)  2=Max(%d)  or enter amount: ",
                 minimum, maximum);

        wmove(w_msg, MSG_LINES - 2, 1);
        wclrtoeol(w_msg);
        mvwaddch(w_msg, MSG_LINES - 2, g_cols - 1, ACS_VLINE);
        SetColor(w_msg, CP_MSG, true);
        mvwaddnstr(w_msg, MSG_LINES - 2, 1, prompt, g_cols - 4);
        SetColor(w_msg, CP_MSG, false);

        int prompt_len = (int)strlen(prompt);
        if (prompt_len > g_cols - 4) prompt_len = g_cols - 4;
        wmove(w_msg, MSG_LINES - 2, 1 + prompt_len);
        wrefresh(w_msg);

        echo();
        curs_set(1);
        wgetnstr(w_msg, buf, (int)sizeof(buf) - 1);
        curs_set(0);
        noecho();

        if (buf[0] == '1' && buf[1] == '\0') return minimum;
        if (buf[0] == '2' && buf[1] == '\0') return maximum;

        val = atoi(buf);
        if (val >= minimum && val <= maximum)
            return val;

        MsgPrint("Enter 1 (min), 2 (max), or a value between %d and %d.",
                 minimum, maximum);
    }
}

/* "Press ENTER to continue" — uses the input line, not the log. */
void MsgWaitEnter(void)
{
    const char *msg = "[ Press ENTER to continue ]";
    wmove(w_msg, MSG_LINES - 2, 1);
    wclrtoeol(w_msg);
    mvwaddch(w_msg, MSG_LINES - 2, g_cols - 1, ACS_VLINE);
    SetColor(w_msg, CP_TITLE, true);
    mvwaddnstr(w_msg, MSG_LINES - 2, 1, msg, g_cols - 2);
    SetColor(w_msg, CP_TITLE, false);
    wrefresh(w_msg);
    wgetch(w_msg);

    /* Clear the input line after the keypress */
    wmove(w_msg, MSG_LINES - 2, 1);
    wclrtoeol(w_msg);
    mvwaddch(w_msg, MSG_LINES - 2, g_cols - 1, ACS_VLINE);
    wrefresh(w_msg);
}

/* ===========================================================================
 * DrawStats — right-hand panel
 * =========================================================================*/
void DrawStats(player *Me)
{
    int row = 1;
    werase(w_stats);
    box(w_stats, 0, 0);

    /* Title bar */
    SetColor(w_stats, CP_TITLE, true);
    mvwprintw(w_stats, row++, 1, " %-*s", g_stat_cols - 3, Me->City);
    mvwprintw(w_stats, row++, 1, " %s %s", Me->Title, Me->Name);
    SetColor(w_stats, CP_TITLE, false);

    mvwhline(w_stats, row++, 1, ACS_HLINE, g_stat_cols - 2);

    /* Helper macro to print a labelled stat row */
#define STAT_ROW(lbl, fmt, val) do {                         \
    SetColor(w_stats, CP_STATS_HDR, true);                   \
    mvwprintw(w_stats, row, 1, "%-14s", (lbl));              \
    SetColor(w_stats, CP_STATS_HDR, false);                  \
    SetColor(w_stats, CP_STATS_VAL, true);                   \
    mvwprintw(w_stats, row, 15, fmt, (val));                 \
    SetColor(w_stats, CP_STATS_VAL, false);                  \
    row++;                                                   \
} while(0)

    STAT_ROW("Year",        "%d",       Me->Year);
    STAT_ROW("Treasury",    "%d fl",    Me->Treasury);
    STAT_ROW("Land",        "%d ha",    Me->Land);
    STAT_ROW("Grain Rsrv",  "%d st",    Me->GrainReserve);
    STAT_ROW("Grain Dmnd",  "%d st",    Me->GrainDemand);
    STAT_ROW("Grain Price", "%d/1000",  Me->GrainPrice);
    STAT_ROW("Land Price",  "%.2f/ha",  Me->LandPrice);

    row++;
    mvwhline(w_stats, row++, 1, ACS_HLINE, g_stat_cols - 2);

    STAT_ROW("Serfs",       "%d",       Me->Serfs);
    STAT_ROW("Soldiers",    "%d",       Me->Soldiers);
    STAT_ROW("Nobles",      "%d",       Me->Nobles);
    STAT_ROW("Clergy",      "%d",       Me->Clergy);
    STAT_ROW("Merchants",   "%d",       Me->Merchants);

    row++;
    mvwhline(w_stats, row++, 1, ACS_HLINE, g_stat_cols - 2);

    STAT_ROW("Customs",     "%d%%",     Me->CustomsDuty);
    STAT_ROW("Sales Tax",   "%d%%",     Me->SalesTax);
    STAT_ROW("Income Tax",  "%d%%",     Me->IncomeTax);

    const char *justiceStr;
    switch (Me->Justice) {
        case 1:  justiceStr = "Very Fair";   break;
        case 2:  justiceStr = "Moderate";    break;
        case 3:  justiceStr = "Harsh";       break;
        default: justiceStr = "Outrageous";  break;
    }
    STAT_ROW("Justice",     "%s",       justiceStr);

    row++;
    mvwhline(w_stats, row++, 1, ACS_HLINE, g_stat_cols - 2);

    STAT_ROW("Cathedrals",  "%d",       Me->Cathedral);
    STAT_ROW("Palaces",     "%d",       Me->Palace);
    STAT_ROW("Markets",     "%d",       Me->Marketplaces);
    STAT_ROW("Mills",       "%d",       Me->Mills);
    STAT_ROW("Pub. Works",  "%.2f",     Me->PublicWorks);

#undef STAT_ROW

    /* Bankruptcy / invasion warnings */
    if (Me->IsBankrupt) {
        SetColor(w_stats, CP_WARNING, true);
        mvwprintw(w_stats, row++, 1, "** BANKRUPT **");
        SetColor(w_stats, CP_WARNING, false);
    }
    if (Me->InvadeMe) {
        SetColor(w_stats, CP_WARNING, true);
        mvwprintw(w_stats, row++, 1, "** UNDER THREAT **");
        SetColor(w_stats, CP_WARNING, false);
    }

    wrefresh(w_stats);
}

/* ===========================================================================
 * DrawMap — left-hand panel
 *
 * The map is divided into three horizontal bands:
 *
 *   [ guard tower | sky / title bar                    ]   <- top ~25%
 *   [             | walled city (buildings inside)     ]   <- middle ~50%
 *   [ fields / plowman                                 ]   <- bottom ~25%
 *
 * Scaling rules (faithful to original description):
 *   Wall width   = scales with Land   (min 10, max map_inner_cols-2)
 *   Wall height  = scales with Land   (min 4,  max city_band_height-2)
 *   Tower height = scales with Soldiers vs Land/1000 adequacy ratio
 *                  (tall = well defended, short = vulnerable)
 *   Tower width  = always 4 chars
 *   Plowman pos  = at top of field band if Serfs >= Land/10 (all in production)
 *                  otherwise descends proportionally
 *   Buildings    = cathedrals (+), palaces (P), markets (M), mills (~)
 *                  drawn left-to-right inside the wall
 * =========================================================================*/
void DrawMap(player *Me)
{
    werase(w_map);
    box(w_map, 0, 0);

    int inner_cols  = g_map_cols  - 2;   /* inside box border */
    int inner_lines = g_map_lines - 2;

    if (inner_cols < 20 || inner_lines < 12) {
        mvwprintw(w_map, 1, 1, "Terminal too small.");
        wrefresh(w_map);
        return;
    }

    /* --- Band heights ---------------------------------------------------- */
    int sky_lines    = inner_lines / 5;           /* sky/title: 20%  */
    int city_lines   = (inner_lines * 2) / 4;     /* city:      50%  */
    int field_lines  = inner_lines - sky_lines - city_lines; /* fields: ~30% */

    int sky_top    = 1;
    int city_top   = sky_top   + sky_lines;
    int field_top  = city_top  + city_lines;

    /* --- Title in sky band ----------------------------------------------- */
    SetColor(w_map, CP_TITLE, true);
    mvwprintw(w_map, sky_top, 2, "%s, %d AD", Me->City, Me->Year);
    SetColor(w_map, CP_TITLE, false);

    /* --- City wall dimensions -------------------------------------------- */
    /* Land ranges roughly 5000–50000; wall width scales across inner_cols */
    int land_clamped = Me->Land;
    if (land_clamped < 5000)  land_clamped = 5000;
    if (land_clamped > 50000) land_clamped = 50000;

    int wall_w = (int)((float)(land_clamped - 5000) /
                       (float)(50000 - 5000) *
                       (float)(inner_cols - 12)) + 10;
    if (wall_w > inner_cols - 2) wall_w = inner_cols - 2;

    int wall_h = (city_lines * wall_w) / (inner_cols - 2);
    if (wall_h < 4)             wall_h = 4;
    if (wall_h > city_lines - 1) wall_h = city_lines - 1;

    int wall_left = (inner_cols - wall_w) / 2 + 1;
    int wall_top  = city_top + (city_lines - wall_h) / 2;

    /* --- Draw city walls ------------------------------------------------- */
    SetColor(w_map, CP_WALL, true);

    /* Top and bottom wall */
    for (int c = wall_left; c < wall_left + wall_w; c++) {
        mvwaddch(w_map, wall_top,              c, ACS_HLINE);
        mvwaddch(w_map, wall_top + wall_h - 1, c, ACS_HLINE);
    }
    /* Left and right wall */
    for (int r = wall_top + 1; r < wall_top + wall_h - 1; r++) {
        mvwaddch(w_map, r, wall_left,                  ACS_VLINE);
        mvwaddch(w_map, r, wall_left + wall_w - 1,     ACS_VLINE);
    }
    /* Corners */
    mvwaddch(w_map, wall_top,              wall_left,                ACS_ULCORNER);
    mvwaddch(w_map, wall_top,              wall_left + wall_w - 1,   ACS_URCORNER);
    mvwaddch(w_map, wall_top + wall_h - 1, wall_left,                ACS_LLCORNER);
    mvwaddch(w_map, wall_top + wall_h - 1, wall_left + wall_w - 1,   ACS_LRCORNER);

    /* Gate in the middle of the bottom wall */
    int gate_col = wall_left + wall_w / 2;
    mvwaddch(w_map, wall_top + wall_h - 1, gate_col,     '[');
    mvwaddch(w_map, wall_top + wall_h - 1, gate_col + 1, ']');

    SetColor(w_map, CP_WALL, false);

    /* --- Guard tower (upper-left of wall) -------------------------------- */
    /*
     * Tower adequacy: soldiers should be >= land/1000.
     * ratio = soldiers / (land/1000).  Clamp 0.0–2.0.
     * Tower height scales from 1 (ratio=0) to sky_lines+2 (ratio>=1.5).
     */
    float tower_ratio = (float)Me->Soldiers /
                        (float)((Me->Land > 0 ? Me->Land : 1) / 1000 + 1);
    if (tower_ratio > 2.0f) tower_ratio = 2.0f;

    int tower_max_h = sky_lines + 2;   /* can extend up into sky band */
    int tower_h     = (int)(tower_ratio / 2.0f * (float)tower_max_h);
    if (tower_h < 1) tower_h = 1;
    int tower_w     = 5;
    int tower_left  = wall_left;
    int tower_base  = wall_top;        /* tower sits on top of wall */
    int tower_top_r = tower_base - tower_h;
    if (tower_top_r < sky_top) tower_top_r = sky_top;

    SetColor(w_map, CP_TOWER, true);
    /* Tower sides */
    for (int r = tower_top_r; r < tower_base; r++) {
        mvwaddch(w_map, r, tower_left,              '|');
        mvwaddch(w_map, r, tower_left + tower_w - 1, '|');
        /* Fill interior */
        for (int c = tower_left + 1; c < tower_left + tower_w - 1; c++)
            mvwaddch(w_map, r, c, ' ');
    }
    /* Battlements on top */
    if (tower_top_r >= sky_top) {
        for (int c = tower_left; c < tower_left + tower_w; c++)
            mvwaddch(w_map, tower_top_r, c, (c % 2 == 0) ? 'n' : '_');
    }
    SetColor(w_map, CP_TOWER, false);

    /* --- Buildings inside the walls -------------------------------------- */
    /*
     * We draw symbols left-to-right on the row just above the bottom wall:
     *   Cathedral -> '+'   Palace -> 'P'   Market -> 'M'   Mill -> '~'
     */
    SetColor(w_map, CP_BUILDINGS, true);
    int brow = wall_top + wall_h - 2;   /* one row above gate */
    int bcol = wall_left + 2;
    int bmax = wall_left + wall_w - 2;

#define DRAW_BUILDINGS(sym, count) do {              \
    for (int _i = 0; _i < (count) && bcol < bmax; _i++, bcol++) \
        mvwaddch(w_map, brow, bcol, (sym));          \
} while(0)

    DRAW_BUILDINGS('+', Me->Cathedral);
    DRAW_BUILDINGS('P', Me->Palace);
    DRAW_BUILDINGS('M', Me->Marketplaces);
    DRAW_BUILDINGS('~', Me->Mills);

#undef DRAW_BUILDINGS

    /* A simple keep/castle in the centre of the wall interior */
    int keep_col = wall_left + wall_w / 2 - 1;
    int keep_row = wall_top  + wall_h / 2;
    if (keep_row < wall_top + 1)             keep_row = wall_top + 1;
    if (keep_row > wall_top + wall_h - 2)    keep_row = wall_top + wall_h - 2;
    if (keep_col > 1 && keep_col + 3 < g_map_cols - 1) {
        mvwprintw(w_map, keep_row - 1, keep_col, "^n^");
        mvwprintw(w_map, keep_row,     keep_col, "[H]");
    }

    SetColor(w_map, CP_BUILDINGS, false);

    /* --- Fields and plowman --------------------------------------------- */
    /*
     * The plowman is at the TOP of the field band when all land is in
     * production (serfs >= land/10).  Otherwise he descends proportionally.
     */
    SetColor(w_map, CP_FIELDS, true);

    /* Draw field rows with crop symbols */
    for (int r = field_top; r < field_top + field_lines - 1; r++) {
        for (int c = 1; c < inner_cols + 1; c++) {
            char ch = ((r + c) % 4 == 0) ? '"' : '.';
            mvwaddch(w_map, r, c, ch);
        }
    }

    /* Plowman position */
    int serfs_needed = Me->Land / 10;
    if (serfs_needed < 1) serfs_needed = 1;
    float prod_ratio = (float)Me->Serfs / (float)serfs_needed;
    if (prod_ratio > 1.0f) prod_ratio = 1.0f;

    /* prod_ratio==1 → row 0 of field band; ratio==0 → last row */
    int plow_row = field_top +
                   (int)((1.0f - prod_ratio) * (float)(field_lines - 2));
    if (plow_row < field_top)                    plow_row = field_top;
    if (plow_row > field_top + field_lines - 2)  plow_row = field_top + field_lines - 2;

    int plow_col = inner_cols / 3;
    /* Clear a little space around the plowman */
    mvwprintw(w_map, plow_row, plow_col, "        ");
    SetColor(w_map, CP_FIELDS, false);

    SetColor(w_map, CP_BUILDINGS, true);
    mvwprintw(w_map, plow_row, plow_col, "o-HH-8>");  /* horse & plowman */
    SetColor(w_map, CP_BUILDINGS, false);

    /* Grain reserve indicator — a simple bar at the very bottom */
    int max_grain = 20000;
    int grain_bar = (int)((float)Me->GrainReserve /
                          (float)(max_grain > 0 ? max_grain : 1) *
                          (float)(inner_cols));
    if (grain_bar > inner_cols) grain_bar = inner_cols;
    if (grain_bar < 0)          grain_bar = 0;

    SetColor(w_map, CP_FIELDS, true);
    for (int c = 1; c <= grain_bar; c++)
        mvwaddch(w_map, field_top + field_lines - 1, c, ACS_BLOCK);
    mvwprintw(w_map, field_top + field_lines - 1,
              grain_bar + 2, "grain:%d", Me->GrainReserve);
    SetColor(w_map, CP_FIELDS, false);

    wrefresh(w_map);
}

/* ===========================================================================
 * Random
 * =========================================================================*/
int Random(int hi)
{
    if (hi <= 0) return 0;
    return rand() % (hi + 1);
}

/* ===========================================================================
 * InitializePlayer
 * =========================================================================*/
void InitializePlayer(player *Me, int year, int city, int level,
                      const char *name, bool MorF)
{
    Me->Cathedral   = 0;
    strncpy(Me->City, CityList[city], sizeof(Me->City) - 1);
    Me->City[sizeof(Me->City) - 1] = '\0';
    Me->Clergy         = 5;
    Me->CustomsDuty    = 25;
    Me->Difficulty     = level;
    Me->GrainPrice     = 25;
    Me->GrainReserve   = 5000;
    Me->IncomeTax      = 5;
    Me->IsBankrupt     = false;
    Me->IsDead         = false;
    Me->IWon           = false;
    Me->Justice        = 2;
    Me->Land           = 10000;
    Me->LandPrice      = 10.0f;
    Me->MaleOrFemale   = MorF;
    Me->Marketplaces   = 0;
    Me->Merchants      = 25;
    Me->Mills          = 0;
    strncpy(Me->Name, name, sizeof(Me->Name) - 1);
    Me->Name[sizeof(Me->Name) - 1] = '\0';
    Me->Nobles         = 4;
    Me->OldTitle       = 1;
    Me->Palace         = 0;
    Me->PublicWorks    = 1.0f;
    Me->SalesTax       = 10;
    Me->Serfs          = 2000;
    Me->Soldiers       = 25;
    Me->TitleNum       = 1;

    if (Me->MaleOrFemale)
        strncpy(Me->Title, MaleTitles[0], sizeof(Me->Title) - 1);
    else
        strncpy(Me->Title, FemaleTitles[0], sizeof(Me->Title) - 1);
    Me->Title[sizeof(Me->Title) - 1] = '\0';

    if (city == 6) strncpy(Me->Title, "Baron", sizeof(Me->Title) - 1);

    Me->Treasury    = 1000;
    Me->WhichPlayer = city;
    Me->Year        = year;
    Me->YearOfDeath = year + 20 + Random(35);
}

/* ===========================================================================
 * AddRevenue
 * =========================================================================*/
void AddRevenue(player *Me)
{
    Me->Treasury += Me->JusticeRevenue  + Me->CustomsDutyRevenue
                  + Me->IncomeTaxRevenue + Me->SalesTaxRevenue;

    if (Me->Treasury < 0)
        Me->Treasury = (int)((float)Me->Treasury * 1.5f);

    if (Me->Treasury < (-10000 * Me->TitleNum))
        Me->IsBankrupt = true;
}

/* ===========================================================================
 * AttackNeighbor
 * =========================================================================*/
int AttackNeighbor(player *Me, player *Him)
{
    int LandTaken, deadsoldiers;

    if (Me->WhichPlayer == 7)
        LandTaken = Random(9000) + 1000;
    else
        LandTaken = (Me->Soldiers * 1000) - (Me->Land / 3);

    if (LandTaken > (Him->Land - 5000))
        LandTaken = (Him->Land - 5000) / 2;

    Me->Land  += LandTaken;
    Him->Land -= LandTaken;

    beep();
    MsgPrint("%s %s of %s invades and seizes %d hectares!",
             Me->Title, Me->Name, Me->City, LandTaken);

    deadsoldiers = Random(40);
    if (deadsoldiers > (Him->Soldiers - 15))
        deadsoldiers = Him->Soldiers - 15;

    Him->Soldiers -= deadsoldiers;
    MsgPrint("%s %s loses %d soldiers in battle.",
             Him->Title, Him->Name, deadsoldiers);

    DrawMap(Him);
    DrawStats(Him);
    return LandTaken;
}

/* ===========================================================================
 * Buy/sell helpers
 * =========================================================================*/
void BuyCathedral(player *Me)
{
    Me->Cathedral++;
    Me->Clergy      += Random(6);
    Me->Treasury    -= 5000;
    Me->PublicWorks += 1.0f;
}

void BuyGrain(player *Me)
{
    int howmuch = MsgGetInt("How much grain to buy (0=specify total)? ", 0, 999999);
    if (howmuch == 0) {
        int total = MsgGetInt("Desired total grain reserve? ",
                              Me->GrainReserve, 999999);
        howmuch = total - Me->GrainReserve;
    }
    Me->Treasury    -= howmuch * Me->GrainPrice / 1000;
    Me->GrainReserve += howmuch;
}

void BuyLand(player *Me)
{
    int howmuch = MsgGetInt("How many hectares to buy? ", 0, 999999);
    Me->Land     += howmuch;
    Me->Treasury -= (int)((float)howmuch * Me->LandPrice);
}

void BuyMarket(player *Me)
{
    Me->Marketplaces++;
    Me->Merchants   += 5;
    Me->Treasury    -= 1000;
    Me->PublicWorks += 1.0f;
}

void BuyMill(player *Me)
{
    Me->Mills++;
    Me->Treasury    -= 2000;
    Me->PublicWorks += 0.25f;
}

void BuyPalace(player *Me)
{
    Me->Palace++;
    Me->Nobles      += Random(2);
    Me->Treasury    -= 3000;
    Me->PublicWorks += 0.5f;
}

void BuySoldiers(player *Me)
{
    Me->Soldiers += 20;
    Me->Serfs    -= 20;
    Me->Treasury -= 500;
}

void SellGrain(player *Me)
{
    int howmuch = MsgGetInt("How much grain to sell? ", 0, Me->GrainReserve);
    Me->Treasury     += howmuch * Me->GrainPrice / 1000;
    Me->GrainReserve -= howmuch;
}

void SellLand(player *Me)
{
    int maxsell = Me->Land - 5000;
    if (maxsell <= 0) {
        MsgPrint("You have no land to sell.");
        return;
    }
    int howmuch = MsgGetInt("How many hectares to sell? ", 0, maxsell);
    Me->Land     -= howmuch;
    Me->Treasury += (int)((float)howmuch * Me->LandPrice);
}

/* ===========================================================================
 * limit10
 * =========================================================================*/
int limit10(int num, int denom)
{
    int val = num / denom;
    return val > 10 ? 10 : val;
}

/* ===========================================================================
 * CheckNewTitle
 * =========================================================================*/
bool CheckNewTitle(player *Me)
{
    int Total = limit10(Me->Marketplaces, 1)
              + limit10(Me->Palace,       1)
              + limit10(Me->Cathedral,    1)
              + limit10(Me->Mills,        1)
              + limit10(Me->Treasury,     5000)
              + limit10(Me->Land,         6000)
              + limit10(Me->Merchants,    50)
              + limit10(Me->Nobles,       5)
              + limit10(Me->Soldiers,     50)
              + limit10(Me->Clergy,       10)
              + limit10(Me->Serfs,        2000)
              + limit10((int)(Me->PublicWorks * 100.0f), 500);

    Me->TitleNum = (Total / Me->Difficulty) - Me->Justice;
    if (Me->TitleNum > 7) Me->TitleNum = 7;
    if (Me->TitleNum < 0) Me->TitleNum = 0;

    if (Me->TitleNum > Me->OldTitle) {
        Me->OldTitle = Me->TitleNum;
        ChangeTitle(Me);
        beep();
        MsgPrint("Good news! %s has achieved the rank of %s!",
                 Me->Name, Me->Title);
        MsgWaitEnter();
        return true;
    }

    Me->TitleNum = Me->OldTitle;
    return false;
}

/* ===========================================================================
 * GenerateHarvest
 * =========================================================================*/
void GenerateHarvest(player *Me)
{
    Me->Harvest     = (Random(5) + Random(6)) / 2;
    Me->Rats        = Random(50);
    Me->GrainReserve = ((Me->GrainReserve * 100) -
                        (Me->GrainReserve * Me->Rats)) / 100;
}

/* ===========================================================================
 * GenerateIncome
 * =========================================================================*/
void GenerateIncome(player *Me)
{
    const char *justStr;
    float y;

    Me->JusticeRevenue = (Me->Justice * 300 - 500) * Me->TitleNum;

    switch (Me->Justice) {
        case 1:  justStr = "Very Fair";   break;
        case 2:  justStr = "Moderate";    break;
        case 3:  justStr = "Harsh";       break;
        default: justStr = "Outrageous";  break;
    }

    y = 150.0f - (float)Me->SalesTax - (float)Me->CustomsDuty
               - (float)Me->IncomeTax;
    if (y < 1.0f) y = 1.0f;
    y /= 100.0f;

    Me->CustomsDutyRevenue = Me->Nobles * 180 + Me->Clergy * 75
                           + Me->Merchants * 20 * (int)y;
    Me->CustomsDutyRevenue += (int)(Me->PublicWorks * 100.0f);
    Me->CustomsDutyRevenue  = (int)((float)Me->CustomsDuty / 100.0f *
                                    (float)Me->CustomsDutyRevenue);

    Me->SalesTaxRevenue = Me->Nobles * 50 + Me->Merchants * 25
                        + (int)(Me->PublicWorks * 10.0f);
    Me->SalesTaxRevenue *= (int)(y * (5 - Me->Justice) * Me->SalesTax);
    Me->SalesTaxRevenue /= 200;

    Me->IncomeTaxRevenue = Me->Nobles * 250
                         + (int)(Me->PublicWorks * 20.0f);
    Me->IncomeTaxRevenue += (int)(10 * Me->Justice * Me->Nobles * y);
    Me->IncomeTaxRevenue *= Me->IncomeTax;
    Me->IncomeTaxRevenue /= 100;

    int revenues = Me->CustomsDutyRevenue + Me->SalesTaxRevenue
                 + Me->IncomeTaxRevenue   + Me->JusticeRevenue;

    MsgPrint("Revenues: %d fl  Customs:%d Sales:%d Income:%d Justice:%d (%s)",
             revenues,
             Me->CustomsDutyRevenue, Me->SalesTaxRevenue,
             Me->IncomeTaxRevenue,   Me->JusticeRevenue,
             justStr);
}

/* ===========================================================================
 * ChangeTitle
 * =========================================================================*/
void ChangeTitle(player *Me)
{
    const char *t = Me->MaleOrFemale ? MaleTitles[Me->TitleNum]
                                     : FemaleTitles[Me->TitleNum];
    strncpy(Me->Title, t, sizeof(Me->Title) - 1);
    Me->Title[sizeof(Me->Title) - 1] = '\0';
    if (Me->TitleNum == 7)
        Me->IWon = true;
}

/* ===========================================================================
 * NewLandAndGrainPrices
 * =========================================================================*/
void NewLandAndGrainPrices(player *Me)
{
    float x, y, myRandom;
    int h;

    myRandom = (float)rand() / (float)RAND_MAX;
    x = (float)Me->Land;
    y = ((float)(Me->Serfs - Me->Mills) * 100.0f) * 5.0f;
    if (y < 0.0f) y = 0.0f;
    if (y < x)    x = y;

    y = (float)Me->GrainReserve * 2.0f;
    if (y < x) x = y;

    y = (float)Me->Harvest + (myRandom - 0.5f);
    h = (int)(x * y);
    Me->GrainReserve += h;

    Me->GrainDemand = Me->Nobles * 100 + Me->Cathedral * 40
                    + Me->Merchants * 30 + Me->Soldiers * 10
                    + Me->Serfs * 5;

    Me->LandPrice = (3.0f * (float)Me->Harvest +
                    (float)Random(6) + 10.0f) / 10.0f;

    if (h < 0) h = -h;

    if (h < 1)
        y = 2.0f;
    else {
        y = (float)Me->GrainDemand / (float)h;
        if (y > 2.0f) y = 2.0f;
    }
    if (y < 0.8f) y = 0.8f;

    Me->LandPrice *= y;
    if (Me->LandPrice < 1.0f) Me->LandPrice = 1.0f;

    Me->GrainPrice = (int)(((6.0f - (float)Me->Harvest) * 3.0f
                           + (float)Random(5) + (float)Random(5))
                           * 4.0f * y);
    Me->RatsAte = h;
}

/* ===========================================================================
 * PrintGrain — now writes to message window
 * =========================================================================*/
void PrintGrain(player *Me)
{
    const char *msg;
    switch (Me->Harvest) {
        case 0:
        case 1: msg = "Drought. Famine threatens.";         break;
        case 2: msg = "Bad weather. Poor harvest.";         break;
        case 3: msg = "Normal weather. Average harvest.";   break;
        case 4: msg = "Good weather. Fine harvest.";        break;
        default: msg = "Excellent weather. Great harvest!"; break;
    }
    MsgPrint("%s (%d steres)", msg, Me->RatsAte);
}

/* ===========================================================================
 * ReleaseGrain
 * =========================================================================*/
int ReleaseGrain(player *Me)
{
    double xp, zp;
    float  x, z;
    int    HowMuch, Maximum, Minimum;

    Minimum = Me->GrainReserve / 5;
    Maximum = Me->GrainReserve - Minimum;

    MsgPrint("Grain demand: %d steres.", Me->GrainDemand);
    HowMuch = MsgGetGrain(Minimum, Maximum);

    Me->SoldierPay = Me->MarketRevenue = Me->NewSerfs = Me->DeadSerfs = 0;
    Me->TransplantedSerfs = Me->FleeingSerfs = 0;
    Me->InvadeMe   = false;
    Me->GrainReserve -= HowMuch;

    z = (float)HowMuch / (float)Me->GrainDemand - 1.0f;
    if (z > 0.0f)  z /= 2.0f;
    if (z > 0.25f) z = z / 10.0f + 0.25f;

    zp = 50.0 - (double)Me->CustomsDuty - (double)Me->SalesTax
             - (double)Me->IncomeTax;
    if (zp < 0.0) zp *= (double)Me->Justice;
    zp /= 10.0;
    if (zp > 0.0) zp += (3.0 - (double)Me->Justice);

    z += (float)(zp / 10.0);
    if (z > 0.5f) z = 0.5f;

    if (HowMuch < (Me->GrainDemand - 1)) {
        x  = ((float)Me->GrainDemand - (float)HowMuch) /
              (float)Me->GrainDemand * 100.0f - 9.0f;
        xp = (double)x;
        if (x > 65.0f) x = 65.0f;
        if (x < 0.0f)  { xp = 0.0; x = 0.0f; }

        SerfsProcreating(Me, 3.0f);
        SerfsDecomposing(Me, (float)xp + 8.0f);
    } else {
        SerfsProcreating(Me, 7.0f);
        SerfsDecomposing(Me, 3.0f);

        if ((Me->CustomsDuty + Me->SalesTax) < 35)
            Me->Merchants += Random(4);

        if (Me->IncomeTax < Random(28)) {
            Me->Nobles += Random(2);
            Me->Clergy += Random(3);
        }

        if (HowMuch > (int)((float)Me->GrainDemand * 1.3f)) {
            zp = (double)Me->Serfs / 1000.0;
            z  = ((float)HowMuch - (float)Me->GrainDemand) /
                  (float)Me->GrainDemand * 10.0f;
            z *= (float)zp * (float)Random(25);
            z += (float)Random(40);
            Me->TransplantedSerfs = (int)z;
            Me->Serfs += Me->TransplantedSerfs;
            MsgPrint("%d serfs move to the city.", Me->TransplantedSerfs);
            zp = (double)z;
            z  = (float)((double)zp * (double)rand() / (double)RAND_MAX);
            if (z > 50.0f) z = 50.0f;
            Me->Merchants += (int)z;
            Me->Nobles++;
            Me->Clergy += 2;
        }
    }

    if (Me->Justice > 2) {
        Me->JusticeRevenue = Me->Serfs / 100
                           * (Me->Justice - 2) * (Me->Justice - 2);
        Me->JusticeRevenue = Random(Me->JusticeRevenue);
        Me->Serfs         -= Me->JusticeRevenue;
        Me->FleeingSerfs   = Me->JusticeRevenue;
        MsgPrint("%d serfs flee harsh justice.", Me->FleeingSerfs);
    }

    Me->MarketRevenue = Me->Marketplaces * 75;
    if (Me->MarketRevenue > 0) {
        Me->Treasury += Me->MarketRevenue;
        MsgPrint("Markets earned %d florins.", Me->MarketRevenue);
    }

    Me->MillRevenue = Me->Mills * (55 + Random(250));
    if (Me->MillRevenue > 0) {
        Me->Treasury += Me->MillRevenue;
        MsgPrint("Woolen mills earned %d florins.", Me->MillRevenue);
    }

    Me->SoldierPay  = Me->Soldiers * 3;
    Me->Treasury   -= Me->SoldierPay;
    MsgPrint("Paid soldiers %d florins.  Serfs in city: %d.",
             Me->SoldierPay, Me->Serfs);

    DrawMap(Me);
    DrawStats(Me);
    MsgWaitEnter();

    if ((Me->Land / 1000) > Me->Soldiers) { Me->InvadeMe = true; return 3; }
    if ((Me->Land / 500)  > Me->Soldiers) { Me->InvadeMe = true; return 3; }
    return 0;
}

/* ===========================================================================
 * SeizeAssets
 * =========================================================================*/
void SeizeAssets(player *Me)
{
    Me->Marketplaces = 0;
    Me->Palace       = 0;
    Me->Cathedral    = 0;
    Me->Mills        = 0;
    Me->Land         = 6000;
    Me->PublicWorks  = 1.0f;
    Me->Treasury     = 100;
    Me->IsBankrupt   = false;

    SetColor(w_msg, CP_WARNING, true);
    MsgPrint("%s %s is BANKRUPT. Creditors seize your assets.", Me->Title, Me->Name);
    SetColor(w_msg, CP_WARNING, false);

    DrawMap(Me);
    DrawStats(Me);
    MsgWaitEnter();
}

/* ===========================================================================
 * Serfs
 * =========================================================================*/
void SerfsDecomposing(player *Me, float scale)
{
    int   absc = (int)scale;
    float ord  = scale - (float)absc;
    Me->DeadSerfs = (int)(((float)Random(absc) + ord) *
                          (float)Me->Serfs / 100.0f);
    Me->Serfs -= Me->DeadSerfs;
    MsgPrint("%d serfs die this year.", Me->DeadSerfs);
}

void SerfsProcreating(player *Me, float scale)
{
    int   absc = (int)scale;
    float ord  = scale - (float)absc;
    Me->NewSerfs = (int)(((float)Random(absc) + ord) *
                         (float)Me->Serfs / 100.0f);
    Me->Serfs += Me->NewSerfs;
    MsgPrint("%d serfs born this year.", Me->NewSerfs);
}

/* ===========================================================================
 * PrintInstructions — displayed before curses starts
 * =========================================================================*/
void PrintInstructions(void)
{
    printf("\nSanta Paravia and Fiumaccio\n\n"
           "You are the ruler of a 15th century Italian city state.\n"
           "If you rule well, you will receive higher titles.  The\n"
           "first player to become king or queen wins.  Life expectancy\n"
           "then was brief, so you may not live long enough to win.\n\n"
           "The map shows your city with its walls, guard tower, and\n"
           "buildings.  The tower height reflects your military strength.\n"
           "The horse and plowman rises toward the wall as more of your\n"
           "land comes into production.  A grain bar at the bottom shows\n"
           "your reserves.  The right panel shows all your vital statistics.\n\n"
           "High taxes raise money but slow growth.  Distribute grain\n"
           "generously to attract serfs and grow your city.\n\n"
           "(Press ENTER to begin)\n");
    fflush(stdout);
    getchar();
}

/* ===========================================================================
 * PlayGame
 * =========================================================================*/
void PlayGame(player players[], int n)
{
    bool   allDead = false, winner = false;
    int    i, winningPlayer = 0;
    player Baron;

    InitializePlayer(&Baron, 1400, 6, 4, "Peppone", true);

    while (!allDead && !winner) {
        for (i = 0; i < n; i++)
            if (!players[i].IsDead)
                NewTurn(&players[i], n, players, &Baron);

        allDead = true;
        for (i = 0; i < n; i++)
            if (!players[i].IsDead) { allDead = false; break; }

        for (i = 0; i < n; i++)
            if (players[i].IWon) { winner = true; winningPlayer = i; }
    }

    MsgClear();
    if (allDead) {
        MsgPrint("The game has ended — all rulers have died.");
    } else {
        beep();
        MsgPrint("GAME OVER.  %s %s WINS!",
                 players[winningPlayer].Title,
                 players[winningPlayer].Name);
    }
    MsgWaitEnter();
}

/* ===========================================================================
 * NewTurn
 * =========================================================================*/
void NewTurn(player *Me, int howMany, player players[], player *Baron)
{
    int i;
    bool invaded = false;

    GenerateHarvest(Me);
    NewLandAndGrainPrices(Me);
    BuySellGrain(Me);
    ReleaseGrain(Me);

    if (Me->InvadeMe) {
        for (i = 0; i < howMany; i++) {
            if (i != Me->WhichPlayer &&
                players[i].Soldiers > (Me->Soldiers * 2.4f)) {
                AttackNeighbor(&players[i], Me);
                invaded = true;
                break;
            }
        }
        if (!invaded)
            AttackNeighbor(Baron, Me);
    }

    AdjustTax(Me);
    DrawMap(Me);
    DrawStats(Me);
    StatePurchases(Me, howMany, players);
    CheckNewTitle(Me);

    Me->Year++;

    if (Me->Year == Me->YearOfDeath)
        ImDead(Me);

    if (Me->TitleNum >= 7)
        Me->IWon = true;
}

/* ===========================================================================
 * BuySellGrain
 * =========================================================================*/
void BuySellGrain(player *Me)
{
    char ch;
    char buf[4];

    while (true) {
        MsgClear();
        MsgPrint("Year %d — %s %s   Rats ate %d%% of grain.",
                 Me->Year, Me->Title, Me->Name, Me->Rats);
        PrintGrain(Me);
        DrawMap(Me);
        DrawStats(Me);

        MsgPrint("1=Buy grain  2=Sell grain  3=Buy land  4=Sell land  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));
        ch = buf[0];

        if (ch == 'q' || ch == 'Q') break;
        if (ch == '1') { BuyGrain(Me);  DrawMap(Me); DrawStats(Me); }
        if (ch == '2') { SellGrain(Me); DrawMap(Me); DrawStats(Me); }
        if (ch == '3') { BuyLand(Me);   DrawMap(Me); DrawStats(Me); }
        if (ch == '4') { SellLand(Me);  DrawMap(Me); DrawStats(Me); }
    }
}

/* ===========================================================================
 * AdjustTax
 * =========================================================================*/
void AdjustTax(player *Me)
{
    char buf[4];
    int  duty;

    while (true) {
        MsgClear();
        GenerateIncome(Me);
        DrawStats(Me);
        MsgPrint("Customs:%d%%  Sales:%d%%  Income:%d%%",
                 Me->CustomsDuty, Me->SalesTax, Me->IncomeTax);
        MsgPrint("1=Customs  2=Sales Tax  3=Wealth Tax  4=Justice  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));

        if (buf[0] == 'q' || buf[0] == 'Q') break;

        switch (atoi(buf)) {
            case 1:
                duty = MsgGetInt("New customs duty (0-100): ", 0, 100);
                Me->CustomsDuty = duty;
                break;
            case 2:
                duty = MsgGetInt("New sales tax (0-50): ", 0, 50);
                Me->SalesTax = duty;
                break;
            case 3:
                duty = MsgGetInt("New wealth tax (0-25): ", 0, 25);
                Me->IncomeTax = duty;
                break;
            case 4:
                duty = MsgGetInt("Justice 1=Very Fair 2=Moderate 3=Harsh 4=Outrageous: ", 1, 4);
                Me->Justice = duty;
                break;
        }
        DrawStats(Me);
    }

    AddRevenue(Me);
    if (Me->IsBankrupt)
        SeizeAssets(Me);
}

/* ===========================================================================
 * StatePurchases
 * =========================================================================*/
void StatePurchases(player *Me, int howMany, player players[])
{
    char buf[4];

    while (true) {
        MsgClear();
        MsgPrint("%s %s — Treasury: %d fl",
                 Me->Title, Me->Name, Me->Treasury);
        MsgPrint("1=Market(1000)  2=Mill(2000)  3=Palace(3000)  4=Cathedral(5000)");
        MsgPrint("5=Soldiers(500)  6=Standings  q=Done");
        MsgGetStr("> ", buf, sizeof(buf));

        if (buf[0] == 'q' || buf[0] == 'Q') break;

        switch (atoi(buf)) {
            case 1: BuyMarket(Me);              break;
            case 2: BuyMill(Me);                break;
            case 3: BuyPalace(Me);              break;
            case 4: BuyCathedral(Me);           break;
            case 5: BuySoldiers(Me);            break;
            case 6: ShowStats(players, howMany); break;
        }
        DrawMap(Me);
        DrawStats(Me);
    }
}

/* ===========================================================================
 * ShowStats
 * =========================================================================*/
void ShowStats(player players[], int howMany)
{
    MsgClear();
    MsgPrint("%-12s %6s %8s %6s %9s %6s %7s %8s",
             "Name", "Nobles", "Soldiers", "Clergy",
             "Merchants", "Serfs", "Land", "Treasury");

    for (int i = 0; i < howMany; i++) {
        MsgPrint("%-12s %6d %8d %6d %9d %6d %7d %8d",
                 players[i].Name,
                 players[i].Nobles,   players[i].Soldiers,
                 players[i].Clergy,   players[i].Merchants,
                 players[i].Serfs,    players[i].Land,
                 players[i].Treasury);
    }
    MsgWaitEnter();
}

/* ===========================================================================
 * ImDead
 * =========================================================================*/
void ImDead(player *Me)
{
    const char *cause;
    beep();
    MsgPrint("Very sad news — %s %s has died", Me->Title, Me->Name);

    if (Me->Year > 1450) {
        cause = "of old age after a long reign.";
    } else {
        switch (Random(8)) {
            case 0: case 1: case 2: case 3:
                cause = "of pneumonia after a cold winter in a drafty castle.";
                break;
            case 4:
                cause = "of typhoid after drinking contaminated water.";
                break;
            case 5:
                cause = "in a smallpox epidemic.";
                break;
            case 6:
                cause = "after being attacked by robbers while travelling.";
                break;
            default:
                cause = "of food poisoning.";
                break;
        }
    }

    MsgPrint("%s", cause);
    Me->IsDead = true;
    MsgWaitEnter();
}

/* ===========================================================================
 * main
 * =========================================================================*/
int main(int argc, char *argv[])
{
    player players[6];
    int    numPlayers = 0, i, level;
    char   name[25], buf[8];
    bool   MorF;

    srand((unsigned)time(NULL));

#ifdef NETWORK_MODE
    /* -------------------------------------------------------------------
     * Network mode argument parsing:
     *   --server <url>   server base URL (default http://localhost:8765)
     *   --join <game_id> join an existing game (non-host players)
     *   --token <token>  player authentication token
     *   --player <N>     which player slot we are (0-based)
     *
     * Without --join this client creates a new game (host flow).
     * ----------------------------------------------------------------- */
    bool net_join = false;
    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--server") == 0 && i+1 < argc)
            strncpy(g_server_url, argv[++i], sizeof(g_server_url)-1);
        else if (strcmp(argv[i], "--join")   == 0 && i+1 < argc) {
            strncpy(g_game_id, argv[++i], sizeof(g_game_id)-1);
            net_join = true;
        } else if (strcmp(argv[i], "--token")  == 0 && i+1 < argc)
            strncpy(g_player_token, argv[++i], sizeof(g_player_token)-1);
        else if (strcmp(argv[i], "--player") == 0 && i+1 < argc)
            g_my_player_index = atoi(argv[++i]);
    }

    if (!net_join) {
        /* ---- HOST FLOW ---- */
        printf("Santa Paravia and Fiumaccio — Network Host\n\n");
        printf("Server: %s\n\n", g_server_url);

        printf("How many players? (1-6): "); fflush(stdout);
        fgets(buf, sizeof(buf), stdin);
        numPlayers = atoi(buf);
        if (numPlayers < 1 || numPlayers > 6) { printf("Thanks for playing.\n"); return 0; }

        printf("Difficulty: 1=Apprentice  2=Journeyman  3=Master  4=Grand Master: ");
        fflush(stdout);
        fgets(buf, sizeof(buf), stdin);
        level = atoi(buf);
        if (level < 1) level = 1;
        if (level > 4) level = 4;

        /* Build JSON body for POST /game/new */
        char json_body[2048];
        int  jlen = snprintf(json_body, sizeof(json_body),
                             "{\"difficulty\":%d,\"players\":[", level);
        for (i = 0; i < numPlayers; i++) {
            printf("Ruler of %s? ", CityList[i]); fflush(stdout);
            fgets(name, sizeof(name), stdin);
            name[strcspn(name, "\n")] = '\0';
            printf("Is %s male or female? (M/F): ", name); fflush(stdout);
            fgets(buf, sizeof(buf), stdin);
            MorF = (buf[0] == 'm' || buf[0] == 'M');
            InitializePlayer(&players[i], 1400, i, level, name, MorF);
            jlen += snprintf(json_body + jlen, sizeof(json_body) - jlen,
                             "%s{\"name\":\"%s\",\"male\":%s}",
                             i ? "," : "", name, MorF ? "true" : "false");
        }
        snprintf(json_body + jlen, sizeof(json_body) - jlen, "]}");

        char url[512];
        snprintf(url, sizeof(url), "%s/game/new", g_server_url);
        char *resp = net_post(url, json_body, NULL);
        if (!resp) {
            fprintf(stderr, "Could not reach server at %s\n", g_server_url);
            return 1;
        }

        json_str(resp, "game_id", g_game_id, sizeof(g_game_id));

        printf("\n=== Game created: %s ===\n", g_game_id);
        printf("Share these join commands with other players:\n\n");

        /* Walk the "tokens" object: {"0":"uuid","1":"uuid",...} */
        const char *tokens_p = strstr(resp, "\"tokens\"");
        for (i = 0; i < numPlayers; i++) {
            char key[8], tok[64] = "";
            snprintf(key, sizeof(key), "%d", i);
            if (tokens_p) {
                /* Find "\"<key>\"" inside tokens object */
                char needle[16];
                snprintf(needle, sizeof(needle), "\"%s\"", key);
                const char *vp = strstr(tokens_p, needle);
                if (vp) {
                    vp = strchr(vp, ':');
                    if (vp) {
                        vp++;
                        while (*vp == ' ' || *vp == '"') vp++;
                        int ti = 0;
                        while (*vp && *vp != '"' && ti < 63)
                            tok[ti++] = *vp++;
                        tok[ti] = '\0';
                    }
                }
            }
            printf("  Player %d (%s):\n"
                   "    ./paravia --server %s --join %s --player %d --token %s\n\n",
                   i, players[i].Name, g_server_url, g_game_id, i, tok);
            if (i == 0) {
                strncpy(g_player_token, tok, sizeof(g_player_token)-1);
                g_my_player_index = 0;
            }
        }
        free(resp);
        printf("(Press ENTER to continue as Player 0)\n");
        fflush(stdout);
        getchar();

    } else {
        /* ---- JOIN FLOW ---- */
        if (g_my_player_index < 0 || g_player_token[0] == '\0') {
            fprintf(stderr, "Network join requires --player N and --token TOKEN\n");
            return 1;
        }
        char url[512];
        snprintf(url, sizeof(url), "%s/game/%s/state", g_server_url, g_game_id);
        char *resp = net_get(url);
        if (!resp) {
            fprintf(stderr, "Could not fetch game state from %s\n", g_server_url);
            return 1;
        }
        int np = 0;
        json_int(resp, "num_players", &np);
        if (np < 1 || np > 6) np = 6;
        numPlayers = np;
        for (i = 0; i < numPlayers; i++)
            InitializePlayer(&players[i], 1400, i, 2, "Unknown", true);
        /* Apply each player's state from the players array in the response */
        const char *pp = resp;
        int pidx = 0;
        while (pidx < numPlayers && (pp = strstr(pp, "\"which_player\"")) != NULL) {
            /* back up to find the opening brace of this player object */
            const char *obj = pp;
            while (obj > resp && *obj != '{') obj--;
            apply_player_state(&players[pidx], obj);
            pidx++;
            pp++;
        }
        free(resp);
        printf("\nJoined game %s as %s (player %d).\n",
               g_game_id, players[g_my_player_index].Name, g_my_player_index);
        printf("(Press ENTER when ready)\n");
        fflush(stdout);
        getchar();
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    InitWindows();
    if (g_cols < MIN_COLS || g_lines < MIN_LINES) {
        TeardownWindows();
        fprintf(stderr, "Terminal must be at least %dx%d.\n", MIN_COLS, MIN_LINES);
        return 1;
    }
    DrawMap(&players[g_my_player_index]);
    DrawStats(&players[g_my_player_index]);
    net_play_game(players, numPlayers, g_my_player_index);
    TeardownWindows();
    curl_global_cleanup();
    return 0;

#else  /* ----------------------------------------------------------------
        * LOCAL SINGLE-PLAYER MODE
        * -------------------------------------------------------------- */

    (void)argc; (void)argv;
    printf("Santa Paravia and Fiumaccio\n\n");
    printf("Do you wish instructions? (Y/N): "); fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) && (buf[0] == 'y' || buf[0] == 'Y'))
        PrintInstructions();

    printf("How many players? (1-6): "); fflush(stdout);
    fgets(buf, sizeof(buf), stdin);
    numPlayers = atoi(buf);
    if (numPlayers < 1 || numPlayers > 6) { printf("Thanks for playing.\n"); return 0; }

    printf("Difficulty: 1=Apprentice  2=Journeyman  3=Master  4=Grand Master: ");
    fflush(stdout);
    fgets(buf, sizeof(buf), stdin);
    level = atoi(buf);
    if (level < 1) level = 1;
    if (level > 4) level = 4;

    for (i = 0; i < numPlayers; i++) {
        printf("Ruler of %s? ", CityList[i]); fflush(stdout);
        fgets(name, sizeof(name), stdin);
        name[strcspn(name, "\n")] = '\0';
        printf("Is %s male or female? (M/F): ", name); fflush(stdout);
        fgets(buf, sizeof(buf), stdin);
        MorF = (buf[0] == 'm' || buf[0] == 'M');
        InitializePlayer(&players[i], 1400, i, level, name, MorF);
    }

    InitWindows();
    if (g_cols < MIN_COLS || g_lines < MIN_LINES) {
        TeardownWindows();
        fprintf(stderr, "Terminal must be at least %dx%d.\n", MIN_COLS, MIN_LINES);
        return 1;
    }
    DrawMap(&players[0]);
    DrawStats(&players[0]);
    PlayGame(players, numPlayers);
    TeardownWindows();
    return 0;

#endif /* NETWORK_MODE */
}
