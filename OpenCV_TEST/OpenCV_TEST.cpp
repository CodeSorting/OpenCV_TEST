#include <opencv2/opencv.hpp>
#include "sqlite3.h"  // ← 따옴표로 변경
#include <string>
#include <vector>
#include <ctime>
#include <iostream>
#include <cstdio>
using namespace cv;
using std::string;
using std::vector;

struct Row {
    int id;
    string username;
    double score;
    string moment;
};

/////////////////////////// SQLite helpers ///////////////////////////
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
        "moment TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ");";
    return db_exec(createSQL);
}

bool db_insert(const string& username, double score) {
    const char* sql = "INSERT INTO scores(username, score) VALUES(?, ?)";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, score);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool db_update(int id, const string& username, double score) {
    const char* sql = "UPDATE scores SET username=?, score=?, moment=CURRENT_TIMESTAMP WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, score);
    sqlite3_bind_int(stmt, 3, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool db_delete(int id) {
    const char* sql = "DELETE FROM scores WHERE id=?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

vector<Row> db_list(int offset, int limit, int& totalCount) {
    vector<Row> rows;
    // totalCount
    const char* cntSql = "SELECT COUNT(*) FROM scores";
    sqlite3_stmt* cstmt;
    totalCount = 0;
    if (sqlite3_prepare_v2(db, cntSql, -1, &cstmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(cstmt) == SQLITE_ROW) totalCount = sqlite3_column_int(cstmt, 0);
    }
    sqlite3_finalize(cstmt);

    const char* listSql =
        "SELECT id, username, score, strftime('%Y-%m-%d %H:%M:%S', moment) "
        "FROM scores ORDER BY score DESC, id ASC LIMIT ? OFFSET ?";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, listSql, -1, &stmt, nullptr) != SQLITE_OK) return rows;
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row r;
        r.id = sqlite3_column_int(stmt, 0);
        r.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        r.score = sqlite3_column_double(stmt, 2);
        r.moment = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rows.push_back(r);
    }
    sqlite3_finalize(stmt);
    return rows;
}

///////////////////////////// UI helpers /////////////////////////////
enum Screen { HOME, GAME, SCOREBOARD, EDIT };
struct Button { Rect rect; string label; };
struct EditField { Rect rect; string label; string value; bool focused = false; bool numeric = false; };

int W = 980, H = 640;
int page = 0, pageSize = 10;
int selectedId = -1;

std::vector<Button> rowUpdateBtns, rowDeleteBtns;
Button btnPrev, btnNext, btnRegister;
Button btnToHome;            // SCOREBOARD → HOME(UI)
Button btnGameStart, btnRanking; // HOME buttons
Button btnGameToHome, btnGameToRanking; // GAME top-right nav

EditField fldUser, fldScore;
Button btnSubmit, btnCancel, btnReset;
bool editModeUpdate = false; // false: insert, true: update

void drawButton(Mat& img, const Button& b, bool /*primary*/ = false) {
    rectangle(img, b.rect, Scalar(60, 120, 220), FILLED);
    rectangle(img, b.rect, Scalar(240, 240, 240), 2);
    int baseline;
    Size ts = getTextSize(b.label, FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);
    Point org(b.rect.x + (b.rect.width - ts.width) / 2, b.rect.y + (b.rect.height + ts.height) / 2 - 2);
    putText(img, b.label, org, FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2, LINE_AA);
}

void drawField(Mat& img, EditField& f) {
    putText(img, f.label, Point(f.rect.x, f.rect.y - 8), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(20, 220, 220), 2, LINE_AA);
    rectangle(img, f.rect, f.focused ? Scalar(0, 220, 220) : Scalar(80, 80, 80), FILLED);
    rectangle(img, f.rect, Scalar(200, 200, 200), 2);
    string text = f.value;
    if (f.focused && ((int)(getTickCount() / getTickFrequency() * 2) % 2 == 0)) text += "|";
    putText(img, text, Point(f.rect.x + 10, f.rect.y + f.rect.height / 2 + 8),
        FONT_HERSHEY_SIMPLEX, 0.7, Scalar(250, 250, 250), 2, LINE_AA);
}

bool inside(const Rect& r, Point p) { return r.contains(p); }

///////////////////////////// Screens ////////////////////////////////
Screen current = HOME;

void showHome() {
    Mat ui(H, W, CV_8UC3, Scalar(16, 20, 32));
    putText(ui, "W E L C O M E", Point(40, 70), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 200, 255), 3, LINE_AA);
    putText(ui, "Choose an option", Point(42, 110), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(220, 220, 220), 2, LINE_AA);

    int bw = 220, bh = 70;
    int cx = W / 2;
    int cy = H / 2 - 20;
    btnGameStart = { Rect(cx - bw - 20, cy - bh / 2, bw, bh), "GAME START" };
    btnRanking = { Rect(cx + 20,        cy - bh / 2, bw, bh), "RANKING" };

    drawButton(ui, btnGameStart, true);
    drawButton(ui, btnRanking, true);

    imshow("Scoreboard", ui);
}

void showGame() {
    Mat ui(H, W, CV_8UC3, Scalar(24, 28, 48));
    putText(ui, "G A M E   S C R E E N", Point(40, 60), FONT_HERSHEY_SIMPLEX, 0.9, Scalar(0, 220, 255), 3, LINE_AA);
    putText(ui, "This is a placeholder for your game UI.", Point(42, 100),
        FONT_HERSHEY_SIMPLEX, 0.7, Scalar(220, 220, 220), 2, LINE_AA);

    // 상단 네비게이션 버튼
    btnGameToHome = { Rect(W - 280, 20, 110, 40), "Home" };
    btnGameToRanking = { Rect(W - 160, 20, 110, 40), "Ranking" };
    drawButton(ui, btnGameToHome);
    drawButton(ui, btnGameToRanking);

    imshow("Scoreboard", ui);
}

void showScoreboard() {
    Mat ui(H, W, CV_8UC3, Scalar(18, 24, 40));
    putText(ui, "S C O R E B O A R D", Point(30, 40), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 180, 255), 3, LINE_AA);

    int total = 0;
    int offset = page * pageSize;
    auto rows = db_list(offset, pageSize, total);

    // table headers
    int x0 = 30, y0 = 70, rowH = 44;
    putText(ui, "#", Point(x0, y0), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(200, 200, 200), 2);
    putText(ui, "Username", Point(x0 + 40, y0), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(200, 200, 200), 2);
    putText(ui, "Score", Point(x0 + 300, y0), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(200, 200, 200), 2);
    putText(ui, "Moment", Point(x0 + 430, y0), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(200, 200, 200), 2);
    putText(ui, "Actions", Point(x0 + 700, y0), FONT_HERSHEY_SIMPLEX, 0.7, Scalar(200, 200, 200), 2);

    rowUpdateBtns.clear(); rowDeleteBtns.clear();

    for (int i = 0;i < (int)rows.size();++i) {
        int y = y0 + 20 + i * rowH;
        const Row& r = rows[i];
        char idx[16];  sprintf_s(idx, sizeof(idx), "%d", offset + i + 1);
        putText(ui, idx, Point(x0, y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(230, 230, 230), 2);
        putText(ui, r.username, Point(x0 + 40, y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255, 230, 180), 2);
        char sv[64];   sprintf_s(sv, sizeof(sv), "%.f", r.score);
        putText(ui, sv, Point(x0 + 300, y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(180, 255, 200), 2);
        putText(ui, r.moment, Point(x0 + 430, y), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(200, 220, 255), 2);

        Button up{ Rect(x0 + 690, y - 22, 80, 28), "Update" };
        Button del{ Rect(x0 + 780, y - 22, 80, 28), "Delete" };
        drawButton(ui, up); drawButton(ui, del);
        rowUpdateBtns.push_back(up); rowDeleteBtns.push_back(del);

        // 얇은 행 구분선
        line(ui, Point(x0, y + 10), Point(W - 40, y + 10), Scalar(40, 60, 80), 1);
    }

    // footer
    char buf[64];
    int totalPages = (total ? ((total - 1) / pageSize + 1) : 0);
    int displayPage = (total ? page + 1 : 0);
    sprintf_s(buf, sizeof(buf), "%d items, page %d/%d", total, displayPage, totalPages);
    putText(ui, buf, Point(30, H - 60), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(120, 220, 220), 2);

    btnPrev = { Rect(30, H - 50, 100, 36), "Prev" };
    btnNext = { Rect(140, H - 50, 100, 36), "Next" };
    btnRegister = { Rect(W - 150, H - 60, 120, 46), "Register" };
    btnToHome = { Rect(W - 290, H - 60, 120, 46), "UI" }; // ← 홈(UI) 화면으로 이동

    drawButton(ui, btnPrev);
    drawButton(ui, btnNext);
    drawButton(ui, btnToHome);
    drawButton(ui, btnRegister, true);

    imshow("Scoreboard", ui);
}

void resetEditFields() {
    fldUser = { Rect(60, 120, 860, 44), "User", "", false, false };
    fldScore = { Rect(60, 210, 860, 44), "Score", "", false, true };
    btnCancel = { Rect(60, 300, 120, 44), "Cancel" };
    btnReset = { Rect(210, 300, 120, 44), "Reset" };
    btnSubmit = { Rect(W - 180, 300, 140, 44), "Submit" };
}

void showEdit() {
    Mat ui(H, W, CV_8UC3, Scalar(22, 28, 52));
    putText(ui, editModeUpdate ? "U P D A T E   S C O R E" : "R E G I S T E R   N E W   S C O R E",
        Point(40, 60), FONT_HERSHEY_SIMPLEX, 0.9, Scalar(0, 200, 255), 3, LINE_AA);

    drawField(ui, fldUser);
    drawField(ui, fldScore);
    drawButton(ui, btnCancel);
    drawButton(ui, btnReset);
    drawButton(ui, btnSubmit, true);
    imshow("Scoreboard", ui);
}

///////////////////////////// Input handling /////////////////////////
void mouseCallback(int event, int x, int y, int /*flags*/, void* /*userdata*/) {
    Point p(x, y);
    if (event != EVENT_LBUTTONDOWN) return;

    if (current == HOME) {
        if (inside(btnGameStart.rect, p)) { current = GAME; showGame(); return; }
        if (inside(btnRanking.rect, p)) { current = SCOREBOARD; page = 0; showScoreboard(); return; }
    }
    else if (current == GAME) {
        if (inside(btnGameToHome.rect, p)) { current = HOME; showHome(); return; }
        if (inside(btnGameToRanking.rect, p)) { current = SCOREBOARD; page = 0; showScoreboard(); return; }
        // 게임 화면 내 추가 인터랙션
    }
    else if (current == SCOREBOARD) {
        // pagination
        if (inside(btnPrev.rect, p)) { page = std::max(0, page - 1); showScoreboard(); return; }
        if (inside(btnNext.rect, p)) { page = page + 1; showScoreboard(); return; }
        if (inside(btnRegister.rect, p)) { current = EDIT; editModeUpdate = false; selectedId = -1; resetEditFields(); showEdit(); return; }
        if (inside(btnToHome.rect, p)) { current = HOME; showHome(); return; } // ← UI로 이동

        // row actions
        {
            int totalDummy; auto rows = db_list(page * pageSize, pageSize, totalDummy);
            for (int i = 0;i < (int)rows.size();++i) {
                if (i < (int)rowUpdateBtns.size() && inside(rowUpdateBtns[i].rect, p)) {
                    // load row -> edit
                    selectedId = rows[i].id;
                    fldUser.value = rows[i].username;
                    char sv[64]; sprintf_s(sv, sizeof(sv), "%.3f", rows[i].score);
                    fldScore.value = sv;
                    current = EDIT; editModeUpdate = true; showEdit(); return;
                }
                if (i < (int)rowDeleteBtns.size() && inside(rowDeleteBtns[i].rect, p)) {
                    db_delete(rows[i].id);
                    showScoreboard(); return;
                }
            }
        }
    }
    else if (current == EDIT) {
        fldUser.focused = inside(fldUser.rect, p);
        fldScore.focused = inside(fldScore.rect, p);
        if (inside(btnCancel.rect, p)) { current = SCOREBOARD; showScoreboard(); return; }
        if (inside(btnReset.rect, p)) { fldUser.value.clear(); fldScore.value.clear(); showEdit(); return; }
        if (inside(btnSubmit.rect, p)) {
            // validation
            if (fldUser.value.empty() || fldScore.value.empty()) { std::cerr << "Empty fields.\n"; return; }
            double s = atof(fldScore.value.c_str());
            if (editModeUpdate && selectedId > 0) db_update(selectedId, fldUser.value, s);
            else db_insert(fldUser.value, s);
            current = SCOREBOARD; showScoreboard(); return;
        }
        showEdit();
    }
}

void handleKey(int key) {
    if (current != EDIT) return;
    EditField* f = fldUser.focused ? &fldUser : (fldScore.focused ? &fldScore : nullptr);
    if (!f) return;

    if (key == 8) { // backspace
        if (!f->value.empty()) f->value.pop_back();
    }
    else if (key == 9) { // tab: switch focus
        if (fldUser.focused) { fldUser.focused = false; fldScore.focused = true; }
        else if (fldScore.focused) { fldScore.focused = false; fldUser.focused = true; }
    }
    else if (key == 13) { // enter
        // 필요 시 제출 동작 연결 가능
    }
    else if (key >= 32 && key <= 126) {
        char c = (char)key;
        if (f->numeric) {
            if ((c >= '0' && c <= '9') || c == '.') f->value.push_back(c);
        }
        else {
            f->value.push_back(c);
        }
    }
    showEdit();
}

int main() {
    if (!db_init()) return -1;

    namedWindow("Scoreboard", WINDOW_AUTOSIZE);
    setMouseCallback("Scoreboard", mouseCallback);

    // 시작은 홈(UI) 화면
    current = HOME;
    showHome();

    for (;;) {
        int key = waitKey(30);
        if (key == 27) break;     // ESC to quit
        if (key != -1) handleKey(key);
    }

    if (db) sqlite3_close(db);
    destroyAllWindows();
    return 0;
}
