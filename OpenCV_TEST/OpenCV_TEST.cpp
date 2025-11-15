#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <queue>
#include <chrono>
#include <thread>      // 스레드 테스트를 위해 추가
#include <iomanip>     // std::setw, std::setprecision
#include <cmath>       // std::abs
#include <algorithm>   // std::max, std::min
#include <random>      // std::mt19937, std::uniform_int_distribution
#include <limits>      // std::numeric_limits
#include <sstream>     // std::stringstream (JSON 빌드용)

// [복원] SQLite3 헤더
#include "sqlite3.h"

// C++ 표준 라이브러리 사용
using std::cout;
using std::cin;
using std::endl;
using std::string;
using std::vector;
using std::map;
using std::priority_queue;

// =================================================================
// 통신 스텁 함수 (담당자: 최주영, 성예찬)
// =================================================================

/**
 * @brief [스텁] 앱으로 JSON 문자열을 전송합니다. (담당: 최주영)
 * @param jsonString 전송할 JSON 데이터
 */
void sendJsonToApp(const string& jsonString) {
    // 이 함수는 '최주영' 이 구현할 실제 통신 코드로 대체됩니다.
    // 테스트 중 전송되는 JSON을 확인하려면 아래 주석을 해제하세요.
    // cout << "[COMM STUB -> APP]: " << jsonString << endl;
}

/**
 * @brief [스텁] 라즈베리파이(RC카)로 제어 값을 전송합니다. (담당: 성예찬)
 * @param handle (핸들 값)
 * @param pedal (페달 값)
 */
void sendControlToRasPi(int handle, int pedal) {
    // 이 함수는 '성예찬' 이 구현할 실제 통신 코드로 대체됩니다.
    // (시나리오 2. 게임 시작 부분)
    // cout << "[COMM STUB -> RPI]: Handle=" << handle << ", Pedal=" << pedal << endl;
}


// =================================================================
// 0. 위반 유형 Enum
// =================================================================
enum ViolationType { SIGNAL, SPEED, WRONG_WAY };


// =================================================================
// 1. 구조체 정의 (DB)
// =================================================================

/**
 * @brief [복원] DB에서 읽어올 행 구조체
 */
struct Row {
    int id;
    string username;
    double score;
    int signal_violations;
    int speed_violations;
    bool wrong_way;
    string moment;
};

/**
 * @brief DB에 저장할 게임 결과 구조체
 */
struct GameResult {
    string username;
    double score = 0;
    int signal_violations = 0;
    int speed_violations = 0;
    bool wrong_way = false; // 게임 오버 요인
};

sqlite3* db = nullptr;

bool db_exec(const char* sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << (errMsg ? errMsg : "") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }
    return true;
}

/**
 * @brief DB 초기화
 */
bool db_init(const string& path = "scoreboard.db") {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "Cannot open DB\n";
        return false;
    }
    const char* createSQL =
        "CREATE TABLE IF NOT EXISTS scores ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "username TEXT NOT NULL,"
        "score REAL NOT NULL,"
        "signal_violations INTEGER DEFAULT 0,"
        "speed_violations INTEGER DEFAULT 0,"
        "wrong_way INTEGER DEFAULT 0,"
        "moment TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";
    return db_exec(createSQL);
}

/**
 * @brief DB 삽입
 */
bool db_insert(const GameResult& result) {
    const char* sql = "INSERT INTO scores(username, score, signal_violations, speed_violations, wrong_way) VALUES(?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "DB Insert Prepare Error: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, result.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, result.score);
    sqlite3_bind_int(stmt, 3, result.signal_violations);
    sqlite3_bind_int(stmt, 4, result.speed_violations);
    sqlite3_bind_int(stmt, 5, result.wrong_way ? 1 : 0); // bool -> int

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        std::cerr << "DB Insert Step Error: " << sqlite3_errmsg(db) << endl;
    }
    sqlite3_finalize(stmt);
    return ok;
}

/**
 * @brief DB 수정
 */
bool db_update(int id, const GameResult& result) {
    const char* sql = "UPDATE scores SET "
        "username=?, score=?, signal_violations=?, speed_violations=?, wrong_way=?, moment=CURRENT_TIMESTAMP "
        "WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "DB Update Prepare Error: " << sqlite3_errmsg(db) << endl;
        return false;
    }

    sqlite3_bind_text(stmt, 1, result.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, result.score);
    sqlite3_bind_int(stmt, 3, result.signal_violations);
    sqlite3_bind_int(stmt, 4, result.speed_violations);
    sqlite3_bind_int(stmt, 5, result.wrong_way ? 1 : 0);
    sqlite3_bind_int(stmt, 6, id); // WHERE 절에 id 바인딩

    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        std::cerr << "DB Update Step Error: " << sqlite3_errmsg(db) << endl;
    }
    sqlite3_finalize(stmt);
    return ok;
}

/**
 * @brief DB 삭제
 */
bool db_delete(int id) {
    const char* sql = "DELETE FROM scores WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

/**
 * @brief DB 목록 조회
 */
vector<Row> db_list(int offset, int limit, int& totalCount) {
    vector<Row> rows;

    const char* cntSql = "SELECT COUNT(*) FROM scores";
    sqlite3_stmt* cstmt;
    totalCount = 0;
    if (sqlite3_prepare_v2(db, cntSql, -1, &cstmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(cstmt) == SQLITE_ROW) totalCount = sqlite3_column_int(cstmt, 0);
    }
    sqlite3_finalize(cstmt);

    const char* listSql =
        "SELECT id, username, score, "
        "signal_violations, speed_violations, wrong_way, " // 추가된 컬럼
        "strftime('%Y-%m-%d %H:%M:%S', moment) "
        "FROM scores ORDER BY score DESC, id ASC LIMIT ? OFFSET ?";

    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, listSql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "DB List Prepare Error: " << sqlite3_errmsg(db) << endl;
        return rows;
    }
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row r;
        r.id = sqlite3_column_int(stmt, 0);
        r.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.score = sqlite3_column_double(stmt, 2);
        r.signal_violations = sqlite3_column_int(stmt, 3);
        r.speed_violations = sqlite3_column_int(stmt, 4);
        r.wrong_way = (sqlite3_column_int(stmt, 5) == 1); // int -> bool
        r.moment = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));

        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}


// =================================================================
// 3. 게임 월드 (맵) 구현 (그래프 기반)
// =================================================================

// 난수 생성기
std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());

enum NodeType {
    STORE,        // 가게 (4개)
    HOUSE,        // 집 (6개)
    INTERSECTION, // 교차로 (신호등 있음)
    STREET        // 일반 도로 (신호등 없음)
};

struct Node {
    int id;
    string name;
    NodeType type;
    bool lightIsGreen; // 신호등 상태 (교차로인 경우)

    Node(int i, string n, NodeType t)
        : id(i), name(n), type(t), lightIsGreen(true) {
    }
};

struct Edge {
    Node* from;
    Node* to;
    double length;     // km
    double speedLimit; // km/h
    bool isOneWay;

    Edge(Node* f, Node* t, double l, double sl, bool ow = false)
        : from(f), to(t), length(l), speedLimit(sl), isOneWay(ow) {
    }
};

class Map {
public:
    vector<Node*> nodes;
    vector<Edge*> edges;
    map<Node*, vector<Edge*>> adj;
    vector<Node*> stores;
    vector<Node*> houses;
    map<string, Node*> nfcTagMap;

    ~Map() {
        for (auto n : nodes) delete n;
        for (auto e : edges) delete e;
    }

    void buildMap() {
        // 1. 노드 생성 (총 16개)
        nodes.push_back(new Node(0, "S1: 뜨끈국밥", STORE));
        nodes.push_back(new Node(1, "S2: 바삭치킨", STORE));
        nodes.push_back(new Node(2, "S3: 달콤케이크", STORE));
        nodes.push_back(new Node(3, "S4: 시원음료", STORE));

        nodes.push_back(new Node(4, "H1: 101동", HOUSE));
        nodes.push_back(new Node(5, "H2: 102동", HOUSE));
        nodes.push_back(new Node(6, "H3: 201동", HOUSE));
        nodes.push_back(new Node(7, "H4: 202동", HOUSE));
        nodes.push_back(new Node(8, "H5: 301동", HOUSE));
        nodes.push_back(new Node(9, "H6: 302동", HOUSE));

        nodes.push_back(new Node(10, "I1: 사거리A", INTERSECTION));
        nodes.push_back(new Node(11, "I2: 사거리B", INTERSECTION));
        nodes.push_back(new Node(12, "I3: 삼거리C", INTERSECTION));
        nodes.push_back(new Node(13, "I4: 삼거리D", INTERSECTION));

        nodes.push_back(new Node(14, "ST1: 중앙로", STREET));
        nodes.push_back(new Node(15, "ST2: 골목길", STREET));

        for (int i = 0; i < 4; ++i) stores.push_back(nodes[i]);
        for (int i = 4; i < 10; ++i) houses.push_back(nodes[i]);

        // NFC 태그 ID 매핑
        nfcTagMap["NFC_S1"] = nodes[0];
        nfcTagMap["NFC_S2"] = nodes[1];
        nfcTagMap["NFC_S3"] = nodes[2];
        nfcTagMap["NFC_S4"] = nodes[3];
        nfcTagMap["NFC_H1"] = nodes[4];
        nfcTagMap["NFC_H2"] = nodes[5];
        nfcTagMap["NFC_H3"] = nodes[6];
        nfcTagMap["NFC_H4"] = nodes[7];
        nfcTagMap["NFC_H5"] = nodes[8];
        nfcTagMap["NFC_H6"] = nodes[9];
        nfcTagMap["NFC_I1"] = nodes[10];
        nfcTagMap["NFC_I2"] = nodes[11];
        nfcTagMap["NFC_I3"] = nodes[12];
        nfcTagMap["NFC_I4"] = nodes[13];
        nfcTagMap["NFC_ST1"] = nodes[14];
        nfcTagMap["NFC_ST2"] = nodes[15];

        // 2. 엣지(도로) 생성
        addEdge(0, 14, 0.2, 40); // S1 <-> ST1
        addEdge(14, 10, 0.3, 50); // ST1 <-> I1
        addEdge(1, 10, 0.4, 50); // S2 <-> I1
        addEdge(10, 11, 1.0, 60); // I1 <-> I2
        addEdge(10, 12, 1.2, 60); // I1 <-> I3
        addEdge(2, 15, 0.3, 40); // S3 <-> ST2
        addEdge(15, 11, 0.3, 50); // ST2 <-> I2
        addEdge(3, 12, 0.7, 50); // S4 <-> I3
        addEdge(11, 13, 1.1, 70); // I2 <-> I4 (고속)
        addEdge(12, 13, 1.3, 70); // I3 <-> I4 (고속)
        addEdge(11, 4, 0.3, 30); // I2 <-> H1 (스쿨존)
        addEdge(11, 5, 0.4, 30); // I2 <-> H2
        addEdge(12, 6, 0.5, 40); // I3 <-> H3
        addEdge(12, 7, 0.4, 40); // I3 <-> H4
        addEdge(13, 8, 0.8, 50); // I4 <-> H5
        addEdge(13, 9, 0.9, 50); // I4 <-> H6
        addEdge(4, 5, 0.2, 30, true); // H1 -> H2 (일방통행)
    }

    void addEdge(int fromId, int toId, double len, double sl, bool oneWay = false) {
        if (fromId >= nodes.size() || toId >= nodes.size()) {
            std::cerr << "[Map Error] Node ID " << fromId << " or " << toId << " is out of bounds." << endl;
            return;
        }
        Node* from = nodes[fromId];
        Node* to = nodes[toId];
        Edge* e1 = new Edge(from, to, len, sl, oneWay);
        edges.push_back(e1);
        adj[from].push_back(e1);

        if (!oneWay) {
            Edge* e2 = new Edge(to, from, len, sl, oneWay);
            edges.push_back(e2);
            adj[to].push_back(e2);
        }
    }
    //신호등 업데이트(통신)
    void updateTrafficLights() {
        for (Node* n : nodes) {
            if (n->type == INTERSECTION) {
                // 1분에서 10초마다 신호 변경
            }
        }
    }
	//NFC 태그 ID로 노드 조회(통신)
    Node* getNodeByNfcTag(const string& tagId) {
        if (nfcTagMap.count(tagId)) {
            return nfcTagMap.at(tagId);
        }
        std::cerr << "[NFC Error] Unknown Tag ID: " << tagId << endl;
        return nullptr;
    }
    //다익스트라 알고리즘
    vector<Edge*> findPath(Node* start, Node* end) {
        map<Node*, double> dist;
        map<Node*, Edge*> cameFromEdge;
        priority_queue<std::pair<double, Node*>,
            vector<std::pair<double, Node*>>,
            std::greater<std::pair<double, Node*>>> pq;

        for (Node* n : nodes) {
            dist[n] = std::numeric_limits<double>::infinity();
        }

        dist[start] = 0;
        pq.push({ 0, start });
        cameFromEdge[start] = nullptr;

        while (!pq.empty()) {
            double d = pq.top().first;
            Node* u = pq.top().second;
            pq.pop();

            if (d > dist[u]) continue;
            if (u == end) break;

            for (Edge* e : adj[u]) {
                Node* v = e->to;
                double weight = e->length;
                if (dist[v] > dist[u] + weight) {
                    dist[v] = dist[u] + weight;
                    cameFromEdge[v] = e;
                    pq.push({ dist[v], v });
                }
            }
        }

        vector<Edge*> path;
        Node* curr = end;
        while (curr != start) {
            Edge* e = cameFromEdge[curr];
            if (!e) return {};
            path.push_back(e);
            curr = e->from;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }
};


// =================================================================
// 4. 게임 로직 (음식, 콜, 플레이어)
// =================================================================

enum FoodType { HOT_SOUP, FRIED, CAKE, COLD_DRINK };

struct Food {
    FoodType type;
    string name;
    double quality; // 100.0 (최상) ~ 0.0 (최하)

    Food(FoodType t, string n) : type(t), quality(100.0), name(n) {}

    void degradeQuality(double driveTimeSec, double accelChange, double cornerSpeed) {
        double timeFactor = 0, accelFactor = 0, cornerFactor = 0;
        double accelPenalty = std::abs(accelChange);
        double cornerPenalty = cornerSpeed;

        switch (type) {
        case HOT_SOUP:
            timeFactor = 0.5; accelFactor = 0.8; cornerFactor = 0.1;
            break;
        case FRIED:
            timeFactor = 0.1; accelFactor = 0.05; cornerFactor = 0.01;
            break;
        case CAKE:
            timeFactor = 0.05; accelFactor = 0.5; cornerFactor = 0.1;
            break;
        case COLD_DRINK:
            timeFactor = 0.1; accelFactor = 0.8; cornerFactor = 0.2;
            break;
        }
        double qualityLoss = (driveTimeSec * timeFactor) +
            (accelPenalty * accelFactor) +
            (cornerPenalty * cornerFactor);
        quality = std::max(0.0, quality - qualityLoss);
    }
};

struct Call {
    int id;
    Node* store;
    Node* house;
    FoodType foodType;
    string foodName;
    double baseFee;
    std::chrono::steady_clock::time_point expiryTime;
    bool isSpecial;

    Call(int i, Node* s, Node* h, double rating) : id(i), store(s), house(h) {
        if (store->name.find("국밥") != string::npos) {
            foodType = HOT_SOUP;
            vector<string> names = { "뜨끈국밥", "얼큰순대국", "든든돼지국밥" }; //이름은 더 생각중입니다. 바뀔 수도 있습니다.
            foodName = names[std::uniform_int_distribution<>(0, (int)names.size() - 1)(rng)];
        }
        else if (store->name.find("치킨") != string::npos) {
            foodType = FRIED;
            vector<string> names = { "바삭치킨", "매콤양념치킨", "간장마늘치킨" };
            foodName = names[std::uniform_int_distribution<>(0, (int)names.size() - 1)(rng)];
        }
        else if (store->name.find("케이크") != string::npos) {
            foodType = CAKE;
            vector<string> names = { "달콤케이크", "생크림케이크", "초코무스" };
            foodName = names[std::uniform_int_distribution<>(0, (int)names.size() - 1)(rng)];
        }
        else {
            foodType = COLD_DRINK;
            vector<string> names = { "시원음료", "아이스 아메리카노", "딸기스무디" };
            foodName = names[std::uniform_int_distribution<>(0, (int)names.size() - 1)(rng)];
        }

        isSpecial = (std::uniform_int_distribution<>(1, 5)(rng) == 1);
        double ratingBonus = (rating - 2.0) * 1000.0;

        if (isSpecial) {
            baseFee = 5000 + ratingBonus;
            expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        else {
            baseFee = 2000 + ratingBonus;
            expiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        }
        baseFee = std::max(1000.0, baseFee);
    }

    double getRemainingTime() const {
        auto now = std::chrono::steady_clock::now();
        auto remaining = std::chrono::duration_cast<std::chrono::duration<double>>(expiryTime - now);
        return std::max(0.0, remaining.count());
    }
};

class Player {
public:
    string name;
    GameResult stats;
    double totalRevenue = 0;
    double totalFines = 0;
    double rating = 3.0;
    Node* currentLocation;
    Food* currentFood = nullptr;
    std::chrono::steady_clock::time_point pickupTime;

    Player(string n, Node* startNode) : name(n), currentLocation(startNode) {
        stats.username = n;
    }

    ~Player() {
        if (currentFood) delete currentFood;
    }

    // 픽업 시 시간을 기록, 앱에 다음 목적지를 전송
    void pickupFood(Food* food, Node* destination) {
        if (currentFood) delete currentFood;
        currentFood = food;
        pickupTime = std::chrono::steady_clock::now();
        cout << " [알림] " << food->name << "을 픽업했습니다. (현재 품질: 100.0)\n";

        // (통신) 앱으로 "배달지로 이동하세요" 메시지 전송
        string jsonMsg = "{\"status\":\"navigate_to_house\", \"houseName\":\"" + destination->name + "\"}";
        sendJsonToApp(jsonMsg);
    }

    void completeDelivery(double driveTimeSec, double finalQuality) {
        double timeScore = std::max(0.0, (300.0 - driveTimeSec) / 300.0);
        double qualityScore = finalQuality / 100.0;
        double satisfaction = (timeScore * 0.5) + (qualityScore * 0.5);
        double satisfactionScore = 1.0 + (satisfaction * 4.0);
        rating = (rating * 0.9) + (satisfactionScore * 0.1);

        cout << " [배달 완료] 고객 만족도: " << (satisfaction * 100.0) << "%\n";
        cout << " [평점 갱신] 현재 평점: " << std::fixed << std::setprecision(2) << rating << endl;

        delete currentFood;
        currentFood = nullptr;
    }

    void applyFine(double amount, string reason) {
        totalFines += amount;
        stats.score = totalRevenue - totalFines;
        cout << " [위반] " << reason << "! 벌금 " << (int)amount << "원 부과.\n";
        cout << " [현재] 총 수익: " << (int)totalRevenue << " | 총 벌금: " << (int)totalFines << " | 최종 점수: " << (int)stats.score << endl;
    }

    void addRevenue(double amount) {
        totalRevenue += amount;
        stats.score = totalRevenue - totalFines;
    }
};


// =================================================================
// 5. 메인 게임 클래스
// =================================================================

class Game {
public:
    Map map;
    Player player;
    vector<Call*> availableCalls;
    Call* activeCall = nullptr;
    std::chrono::steady_clock::time_point gameStartTime;
    std::chrono::seconds gameDuration = std::chrono::seconds(300); // 5분
    bool gameRunning = true;
    Node* lastKnownNode = nullptr;
    std::chrono::steady_clock::time_point lastDriveUpdateTime;

    Game(string playerName) : player(playerName, nullptr) {
        map.buildMap();
        player.currentLocation = map.stores[0];
        lastKnownNode = player.currentLocation;
        lastDriveUpdateTime = std::chrono::steady_clock::now();
    }

    ~Game() {
        for (auto call : availableCalls) delete call;
        if (activeCall) delete activeCall;
    }

    double getRemainingGameTime() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - gameStartTime);
        return std::max(0.0, gameDuration.count() - elapsed.count());
    }

    GameResult run() {
        gameStartTime = std::chrono::steady_clock::now();
        cout << "=================================================\n";
        cout << "       배달의 전설 (운영 모듈) - " << player.name << " 님\n";
        cout << "       (5분 타이머 시작 / 하드웨어/앱 연동 대기 중...)\n";
        cout << "=================================================\n";

        while (gameRunning) {
            if (getRemainingGameTime() <= 0) {
                cout << "\n[게임 종료] 5분이 모두 경과했습니다.\n";
                gameRunning = false;
                break;
            }
            map.updateTrafficLights();
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 0.1초 틱
        }
        player.stats.score = player.totalRevenue - player.totalFines;
        return player.stats;
    }

    void updateCalls() {
        auto it = availableCalls.begin();
        while (it != availableCalls.end()) {
            if ((*it)->getRemainingTime() <= 0) {
                cout << " [콜 만료] 콜 ID " << (*it)->id << " (만료)\n";
                delete* it;
                it = availableCalls.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // 새 콜 생성 (JSON 전송 로직 포함)
    void generateCalls() {
        updateCalls();

        string jsonOutput = "{";
        int callCount = 0;

        while (availableCalls.size() < 3) {
            Node* store = map.stores[std::uniform_int_distribution<>(0, 3)(rng)];
            Node* house = map.houses[std::uniform_int_distribution<>(0, 5)(rng)];
            int callId = (int)(std::chrono::steady_clock::now().time_since_epoch().count() % 10000);
            Call* newCall = new Call(callId, store, house, player.rating);

            vector<Edge*> pathToStore = map.findPath(player.currentLocation, store);
            vector<Edge*> pathToHouse = map.findPath(store, house);

            double distToStore = 0;
            int lightsToStore = 0;
            for (Edge* e : pathToStore) {
                distToStore += e->length;
                if (e->to->type == INTERSECTION) lightsToStore++;
            }
            double distToHouse = 0;
            int lightsToHouse = 0;
            for (Edge* e : pathToHouse) {
                distToHouse += e->length;
                if (e->to->type == INTERSECTION) lightsToHouse++;
            }
            double totalDist = distToStore + distToHouse;
            int totalLights = lightsToStore + lightsToHouse;

            availableCalls.push_back(newCall);
            callCount++;

            // --- JSON 문자열 구성 
            string callKey = "\"store" + std::to_string(callCount) + "\"";
            jsonOutput += callKey + ": {";
            jsonOutput += "\"id\": " + std::to_string(newCall->id) + ", ";
            jsonOutput += "\"name\": \"" + newCall->store->name + "\", ";
            jsonOutput += "\"foodName\": \"" + newCall->foodName + "\", ";
            jsonOutput += "\"destination\": \"" + newCall->house->name + "\", ";
            jsonOutput += "\"price\": " + std::to_string((int)newCall->baseFee) + ", ";
            std::stringstream ss;
            ss << std::fixed << std::setprecision(1) << totalDist;
            jsonOutput += "\"distance\": " + ss.str() + ", ";
            jsonOutput += "\"lights\": " + std::to_string(totalLights) + ", ";
            jsonOutput += "\"isSpecial\": ";
            jsonOutput += (newCall->isSpecial ? "true" : "false");
            jsonOutput += ", ";
            jsonOutput += "\"expiresIn\": " + std::to_string(newCall->getRemainingTime());
            jsonOutput += "}";

            if (availableCalls.size() < 3) {
                jsonOutput += ", ";
            }

            // --- 콘솔 로그 (테스트용) ---
            cout << " [새 콜 생성] " << (newCall->isSpecial ? "✨특수콜✨" : "일반콜") << "\n";
            cout << "   (ID: " << newCall->id << ") " << newCall->foodName << " (" << newCall->store->name << " -> " << newCall->house->name << ")\n";
            cout << "   (앱 전송 정보: 배달비 " << (int)newCall->baseFee
                << "원, 총 거리 " << std::fixed << std::setprecision(1) << totalDist << "km"
                << ", 신호 " << totalLights << "개)\n";
        }
        jsonOutput += "}";

        // (통신) 3개의 콜 정보를 JSON으로 앱에 전송
        sendJsonToApp(jsonOutput);
        cout << " [통신] 3개의 콜 정보를 앱으로 전송했습니다.\n";
    }

    // -----------------------------------------------------------------
    // 6. 핵심: 외부 이벤트 핸들러
    // -----------------------------------------------------------------

    void OnCallAccepted(int callId) {
        if (activeCall || player.currentFood) {
            cout << "[Error] 이미 다른 콜을 수행 중입니다.\n";
            sendJsonToApp("{\"error\":\"already_on_delivery\", \"message\":\"이미 다른 콜을 수행 중입니다.\"}");
            return;
        }

        Call* foundCall = nullptr;
        int foundIndex = -1;
        for (int i = 0; i < availableCalls.size(); ++i) {
            if (availableCalls[i]->id == callId) {
                foundCall = availableCalls[i];
                foundIndex = i;
                break;
            }
        }

        if (foundCall && foundCall->getRemainingTime() > 0) {
            activeCall = foundCall;
            availableCalls.erase(availableCalls.begin() + foundIndex);

            cout << "[콜 수락] ID: " << activeCall->id << "\n";
            cout << "   " << activeCall->store->name << " (으)로 이동하세요.\n";

            string jsonMsg = "{\"status\":\"navigate_to_store\", \"storeName\":\"" + activeCall->store->name + "\"}";
            sendJsonToApp(jsonMsg);

            cout << " [알림] 선택한 콜 외의 나머지 콜을 목록에서 삭제합니다.\n";
            for (auto call : availableCalls) {
                delete call;
            }
            availableCalls.clear();
        }
        else {
            cout << "[Error] 콜 ID " << callId << "을(를) 찾을 수 없거나 만료되었습니다.\n";
            sendJsonToApp("{\"error\":\"call_expired\", \"message\":\"선택한 콜이 만료되었습니다.\"}");
        }
    }

    void OnNfcTagRead(string tagId) {
        if (!gameRunning) return;

        Node* currentNode = map.getNodeByNfcTag(tagId);
        if (!currentNode) return;

        player.currentLocation = currentNode;
        cout << "[NFC] 현위치: " << currentNode->name << endl;
        lastKnownNode = currentNode;

        if (activeCall && !player.currentFood) {
            if (currentNode == activeCall->store) {
                // [수정] Player::pickupFood에 다음 목적지(집) 정보 전달
                player.pickupFood(
                    new Food(activeCall->foodType, activeCall->foodName),
                    activeCall->house
                );
            }
        }
        else if (activeCall && player.currentFood) {
            if (currentNode == activeCall->house) {
                auto deliveryDuration = std::chrono::steady_clock::now() - player.pickupTime;
                double driveTimeSec = std::chrono::duration_cast<std::chrono::duration<double>>(deliveryDuration).count();

                player.addRevenue(activeCall->baseFee);
                cout << " [수익] 배달비 " << (int)activeCall->baseFee << "원 획득!\n";
                player.completeDelivery(driveTimeSec, player.currentFood->quality);

                delete activeCall;
                activeCall = nullptr;
                sendJsonToApp("{\"status\":\"delivery_complete\", \"waiting_for_call\":true}");

                cout << " [알림] 배달 완료! 새 배달 콜을 생성합니다.\n";
                generateCalls();
            }
        }
    }

    void OnViolationDetected(ViolationType type) {
        if (!gameRunning) return;

        switch (type) {
        case SIGNAL:
            player.applyFine(70000, "신호 위반");
            player.stats.signal_violations++;
            sendJsonToApp("{\"violation\":\"signal\", \"fine\":70000}");
            break;
        case SPEED:
            player.applyFine(40000, "속도 위반");
            player.stats.speed_violations++;
            sendJsonToApp("{\"violation\":\"speed\", \"fine\":40000}");
            break;
        case WRONG_WAY:
            cout << " [역주행] 치명적인 사고 발생! 게임이 종료됩니다.\n";
            player.stats.wrong_way = true;
            gameRunning = false;
            sendJsonToApp("{\"violation\":\"wrong_way\", \"status\":\"game_over\"}");
            break;
        }
    }

    void OnDrivingDataUpdate(double currentSpeed, double accelChange, double cornerSpeed) {
        if (!gameRunning || !player.currentFood) return;

        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::duration<double>>(now - lastDriveUpdateTime);
        double dt_sec = dt.count();

        player.currentFood->degradeQuality(dt_sec, accelChange, cornerSpeed);
        lastDriveUpdateTime = now;
    }
};


// =================================================================
// 7. Main 함수 (테스트 환경)
// =================================================================

int main() {
    if (!db_init("scoreboard.db")) {
        std::cerr << "데이터베이스 초기화 실패!" << endl;
        return 1;
    }

    // 2. 게임 준비 (시나리오 1. 반영)
    string username = "";
    char c;
    cout << "앱에서 '시작' 버튼을 누르기를 기다립니다...\n";
    cout << "(테스트: Enter 키 입력): ";

    string tempInput;
    std::getline(cin, tempInput);

    // (통신) 
    cout << " -> (앱으로 '{\"gameStart\":\"1\"}' 신호 전송)\n";
    sendJsonToApp("{\"gameStart\":\"1\"}");

    cout << "사용자 이름을 한 글자씩 입력하세요 (Enter로 완료):\n";
    while (cin.get(c) && c != '\n') {
        username += c;
        cout << " -> (입력: '" << c << "')\n";
    }
    if (username.empty()) username = "익명의 신입";

    // (통신)
    cout << "[이름 확인] " << username << " 님. (앱으로 'Enter' 및 이름 전송)\n";
    sendJsonToApp("{\"username\":\"" + username + "\"}");

    // 3. 게임 생성
    Game game(username);

    // 4. 게임 루프를 별도 스레드에서 실행
    std::thread gameThread([&game]() {
        game.run();
        });

    // 5. 게임 시작 시 최초 3개의 콜을 생성
    game.generateCalls();

    // 6. [테스트] 메인 스레드에서 가상 이벤트 주입
    cout << "\n[테스트 시뮬레이션 시작]\n";
    cout << " (q: 종료, c: 콜 수락, n: NFC 태그, v: 위반)\n";

    char testInput;
    while (game.gameRunning && cin >> testInput) {
        if (testInput == 'q') {
            game.gameRunning = false;
            break;
        }
        try {
            if (testInput == 'c') { // 'c' (가장 빠른 콜 자동 수락 : '성예찬'이 입력 개발이 끝날 시 더 추가할 예정.)
                if (game.availableCalls.empty()) {
                    cout << " [테스트] 수락할 콜이 없습니다.\n";
                    continue;
                }
                int callId = game.availableCalls[0]->id;
                cout << " [테스트] 첫번째 콜 " << callId << " 수락)\n";
                game.OnCallAccepted(callId);
            }
            if (testInput == 'n') { // 'n' <tag_id_str> (예: n NFC_S1)
                string tag;
                cout << " [테스트] 스캔할 NFC 태그 (예: NFC_S1, NFC_ST1, NFC_H1 ...): ";
                cin >> tag;
                game.OnNfcTagRead(tag);
            }
            if (testInput == 'v') { // 'v' <1,2,3> (Signal, Speed, WrongWay)
                int vType;
                cout << " [테스트] 위반 유형 (1:신호, 2:속도, 3:역주행): ";
                cin >> vType;
                if (vType == 1) game.OnViolationDetected(ViolationType::SIGNAL);
                else if (vType == 2) game.OnViolationDetected(ViolationType::SPEED);
                else if (vType == 3) game.OnViolationDetected(ViolationType::WRONG_WAY);
            }
        }
        catch (const std::exception& e) {
            cout << "[Test Error] " << e.what() << endl;
            cin.clear();
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
        // '성예찬'의 모듈을 시뮬레이션 (0.5초마다 50km/h 정속 주행)
        game.OnDrivingDataUpdate(50.0, 0.0, 0.0);
    }

    // 7. 게임 스레드 조인
    if (gameThread.joinable()) {
        gameThread.join();
    }

    GameResult result = game.player.stats;

    // 8. 게임 종료 및 결과 (출력)
    cout << "\n=============== 게임 결과 ===============\n";
    cout << " 라이더: " << result.username << "\n";
    cout << " 총 수익: " << (int)game.player.totalRevenue << "\n";
    cout << " 총 벌금: " << (int)game.player.totalFines << "\n";
    cout << " 최종 점수: " << (int)result.score << "\n";
    cout << " --------------------------------------\n";
    cout << " 신호 위반: " << result.signal_violations << "회\n";
    cout << " 속도 위반: " << result.speed_violations << "회\n";
    cout << " 역주행: " << (result.wrong_way ? "예 (게임오버)" : "아니오") << "\n";
    cout << "=======================================\n";

    // 9. DB 저장
    if (db_insert(result)) {
        cout << "게임 결과가 스코어보드에 저장되었습니다.\n";
    }
    else {
        cout << "스코어보드 저장에 실패했습니다.\n";
    }

    // 10. 랭킹 표시
    cout << "\n=============== 전체 랭킹 (Top 10) ===============\n";
    int totalCount = 0;
    vector<Row> ranking = db_list(0, 10, totalCount);

    cout << " (총 " << totalCount << "명의 기록)\n";
    cout << "--------------------------------------------------\n";
    cout << std::setw(3) << "순위" << " | "
        << std::setw(15) << "아이디" << " | "
        << std::setw(10) << "점수" << " | "
        << std::setw(19) << "기록 시간" << "\n";
    cout << "--------------------------------------------------\n";

    for (int i = 0; i < ranking.size(); ++i) {
        cout << std::setw(3) << (i + 1) << " | "
            << std::setw(15) << ranking[i].username << " | "
            << std::setw(10) << (int)ranking[i].score << " | "
            << std::setw(19) << ranking[i].moment << "\n";
    }

    // 11. DB 종료
    sqlite3_close(db);

    return 0;
}