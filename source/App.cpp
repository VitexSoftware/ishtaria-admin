#define Uses_TApplication
#define Uses_TDeskTop
#define Uses_TDialog
#define Uses_TButton
#define Uses_TStaticText
#define Uses_TScrollBar
#define Uses_TListViewer
#define Uses_TRect
#define Uses_TEvent
#define Uses_TKeys
#define Uses_TMenuBar
#define Uses_TSubMenu
#define Uses_TMenuItem
#define Uses_TStatusLine
#define Uses_TStatusItem
#define Uses_TStatusDef
#define Uses_MsgBox
#define Uses_TProgram
#include <tvision/tv.h>

#include "ishtariaadmin/App.h"
#include "ishtariaadmin/Ops.h"
#include "ishtariaadmin/i18n.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ishtariaadmin {

namespace {

enum : ushort {
    cmMaps = 1001,
    cmUsers,
    cmPortals,
    cmServer,
    cmAction = 1100, // + action index
};

void showError(const std::string &text) { messageBox(text.c_str(), mfError | mfOKButton); }

bool confirm(const std::string &text) {
    return messageBox(text.c_str(), mfConfirmation | mfYesButton | mfNoButton) == cmYes;
}

std::optional<std::string> ask(const std::string &title, const std::string &label, const std::string &initial = "") {
    char buffer[256] = {};
    initial.copy(buffer, sizeof(buffer) - 1);
    if (inputBox(title.c_str(), label.c_str(), buffer, sizeof(buffer) - 1) != cmOK) {
        return std::nullopt;
    }
    return std::string(buffer);
}

std::string pad(const std::string &text, std::size_t width) {
    return text.size() >= width ? text.substr(0, width) : text + std::string(width - text.size(), ' ');
}

// List viewer that renders externally owned text rows.
class RowList : public TListViewer {
public:
    RowList(const TRect &bounds, TScrollBar *bar) : TListViewer(bounds, 1, nullptr, bar) {}
    void setRows(std::vector<std::string> rows) {
        rows_ = std::move(rows);
        setRange(static_cast<short>(rows_.size()));
        if (focused >= range) {
            focusItem(range > 0 ? range - 1 : 0);
        }
        drawView();
    }
    void getText(char *dest, short item, short maxLen) override {
        std::string text = item >= 0 && item < static_cast<short>(rows_.size()) ? rows_[item] : "";
        std::snprintf(dest, maxLen, "%s", text.c_str());
    }

private:
    std::vector<std::string> rows_;
};

struct Action {
    std::string label;
    std::function<void(long selected)> run; // selected = row index or -1
    bool needsSelection = true;
};

// Modal dialog: header, list of rows and a row of action buttons.
class ListDialog : public TDialog {
public:
    ListDialog(const std::string &title, std::function<std::string()> header,
               std::function<std::vector<std::string>()> rows, std::vector<Action> actions)
        : TDialog(TRect(0, 0, 78, 22), title.c_str()), TWindowInit(&TDialog::initFrame),
          header_(std::move(header)), rows_(std::move(rows)), actions_(std::move(actions)) {
        options |= ofCentered;
        auto *bar = new TScrollBar(TRect(75, 4, 76, 16));
        insert(bar);
        list_ = new RowList(TRect(2, 4, 75, 16), bar);
        insert(list_);
        int index = 0;
        for (const auto &action : actions_) {
            const int col = index % 4, row = index / 4;
            insert(new TButton(TRect(2 + col * 18, 17 + row * 2, 19 + col * 18, 19 + row * 2),
                               action.label.c_str(), static_cast<ushort>(cmAction + index), bfNormal));
            ++index;
        }
        insert(new TButton(TRect(2 + 3 * 18, 19, 19 + 3 * 18, 21), _("~C~lose"), cmCancel, bfNormal));
        reload();
    }

    void handleEvent(TEvent &event) override {
        TDialog::handleEvent(event);
        if (event.what == evCommand && event.message.command >= cmAction &&
            event.message.command < cmAction + actions_.size()) {
            const auto &action = actions_[event.message.command - cmAction];
            clearEvent(event);
            const long selected = rowCount() > 0 ? list_->focused : -1;
            if (action.needsSelection && selected < 0) {
                showError(_("Select an item first."));
                return;
            }
            try {
                action.run(selected);
            } catch (const std::exception &e) {
                showError(e.what());
            }
            reload();
        }
    }

    void reload() {
        try {
            if (headerText_ != nullptr) {
                destroy(headerText_);
            }
            headerText_ = new TStaticText(TRect(2, 2, 76, 4), header_().c_str());
            insert(headerText_);
            const auto rows = rows_();
            list_->setRows(rows);
        } catch (const std::exception &e) {
            showError(e.what());
        }
    }

private:
    int rowCount() const { return list_->range; }
    TStaticText *headerText_ = nullptr;
    RowList *list_;
    std::function<std::string()> header_;
    std::function<std::vector<std::string>()> rows_;
    std::vector<Action> actions_;
};

} // namespace

class AdminApp : public TApplication {
public:
    explicit AdminApp(Ops &ops) : TProgInit(&AdminApp::initStatusLine, &AdminApp::initMenuBar, &TApplication::initDeskTop), ops_(ops) {}

    void handleEvent(TEvent &event) override {
        TApplication::handleEvent(event);
        if (event.what != evCommand) {
            return;
        }
        switch (event.message.command) {
        case cmMaps: mapsDialog(); break;
        case cmUsers: usersDialog(); break;
        case cmPortals: portalsDialog(); break;
        case cmServer: serverDialog(); break;
        default: return;
        }
        clearEvent(event);
    }

    static TMenuBar *initMenuBar(TRect r) {
        r.b.y = r.a.y + 1;
        return new TMenuBar(r,
            *new TSubMenu(_("~A~dministration"), kbAltA) +
                *new TMenuItem(_("World ~m~aps..."), cmMaps, kbF2, hcNoContext, "F2") +
                *new TMenuItem(_("~P~layers..."), cmUsers, kbF3, hcNoContext, "F3") +
                *new TMenuItem(_("~L~inked worlds..."), cmPortals, kbF4, hcNoContext, "F4") +
                *new TMenuItem(_("~S~erver..."), cmServer, kbF5, hcNoContext, "F5") +
                newLine() +
                *new TMenuItem(_("E~x~it"), cmQuit, kbAltX, hcNoContext, "Alt-X"));
    }

    static TStatusLine *initStatusLine(TRect r) {
        r.a.y = r.b.y - 1;
        return new TStatusLine(r,
            *new TStatusDef(0, 0xFFFF) +
                *new TStatusItem(_("~F2~ Maps"), kbF2, cmMaps) +
                *new TStatusItem(_("~F3~ Players"), kbF3, cmUsers) +
                *new TStatusItem(_("~F4~ Linked worlds"), kbF4, cmPortals) +
                *new TStatusItem(_("~F5~ Server"), kbF5, cmServer) +
                *new TStatusItem(_("~Alt-X~ Exit"), kbAltX, cmQuit));
    }

private:
    static long idOf(const Rows &rows, long index) { return std::stol(rows.at(static_cast<std::size_t>(index))[0]); }

    void showDialog(ListDialog *dialog) { deskTop->execView(dialog); destroy(dialog); }

    void mapsDialog() {
        auto cache = std::make_shared<Rows>();
        showDialog(new ListDialog(
            _("World maps"),
            [this] {
                const Row a = ops_.activeMap();
                return a[0].empty() ? std::string(_("Active map: none imported"))
                                    : std::string(_("Active map: seed ")) + a[0] + ", " + a[1] + " px, sha256 " + a[2];
            },
            [this, cache] {
                *cache = ops_.listMaps();
                std::vector<std::string> out;
                for (const auto &r : *cache) {
                    out.push_back(pad(r[1], 28) + pad(r[2], 12) + pad(r[3], 6) + pad(r[4], 14) + r[5]);
                }
                return out;
            },
            {
                {_("~S~ave active"), [this](long) {
                     if (auto name = ask(_("Save map"), _("Name in the library:"))) {
                         ops_.saveActiveMap(*name);
                     }
                 }, false},
                {_("~L~oad"), [this, cache](long i) {
                     const long id = idOf(*cache, i);
                     if (!confirm(_("Replace the active world map? Restart the server afterwards."))) {
                         return;
                     }
                     try {
                         ops_.loadMap(id, false);
                     } catch (const OpError &e) {
                         if (confirm(std::string(e.what()) + "\n" + _("Load anyway?"))) {
                             ops_.loadMap(id, true);
                         }
                     }
                 }},
                {_("~R~ename"), [this, cache](long i) {
                     if (auto name = ask(_("Rename map"), _("New name:"), (*cache).at(i)[1])) {
                         ops_.renameMap(idOf(*cache, i), *name);
                     }
                 }},
                {_("~D~elete"), [this, cache](long i) {
                     if (confirm(_("Delete this saved map?"))) {
                         ops_.deleteMap(idOf(*cache, i));
                     }
                 }},
                {_("~E~xport .pgm"), [this, cache](long i) {
                     if (auto path = ask(_("Export map"), _("Target file:"), "/tmp/" + (*cache).at(i)[1] + ".pgm")) {
                         ops_.exportMap(idOf(*cache, i), *path);
                     }
                 }},
            }));
    }

    void usersDialog() {
        auto cache = std::make_shared<Rows>();
        showDialog(new ListDialog(
            _("Players"), [] { return std::string(_("States: alive, dead, banned")); },
            [this, cache] {
                *cache = ops_.listPlayers();
                std::vector<std::string> out;
                for (const auto &r : *cache) {
                    out.push_back(pad(r[1], 34) + pad(r[2], 9) + pad(r[3], 12) + r[4]);
                }
                return out;
            },
            {
                {_("~R~ename"), [this, cache](long i) {
                     if (auto name = ask(_("Rename player"), _("New name:"), (*cache).at(i)[1])) {
                         ops_.renamePlayer(idOf(*cache, i), *name);
                     }
                 }},
                {_("~B~an"), [this, cache](long i) {
                     if (auto reason = ask(_("Ban player"), _("Reason (optional):"))) {
                         ops_.banPlayer(idOf(*cache, i), *reason);
                     }
                 }},
                {_("~U~nban"), [this, cache](long i) { ops_.unbanPlayer(idOf(*cache, i)); }},
                {_("~D~elete"), [this, cache](long i) {
                     if (confirm(std::string(_("Delete player ")) + (*cache).at(i)[1] + "?")) {
                         ops_.deletePlayer(idOf(*cache, i));
                     }
                 }},
            }));
    }

    void portalsDialog() {
        auto cache = std::make_shared<Rows>();
        auto setState = [this, cache](const char *state) {
            return [this, cache, state](long i) { ops_.setPortalState(idOf(*cache, i), state); };
        };
        showDialog(new ListDialog(
            _("Linked worlds - portals"),
            [] { return std::string(_("Portals are managed here; the server does not enforce them yet (federation is planned).")); },
            [this, cache] {
                *cache = ops_.listPortals();
                std::vector<std::string> out;
                for (const auto &r : *cache) {
                    out.push_back(pad(r[1], 24) + pad(r[2], 26) + pad(r[3], 10) + r[4] + "/" + r[5] + "," + r[6]);
                }
                return out;
            },
            {
                {_("~N~ew"), [this](long) {
                     auto name = ask(_("New portal"), _("Name (a-z, 0-9, -):"));
                     if (!name) return;
                     auto peer = ask(_("New portal"), _("Peer world DNS name (optional):"));
                     if (!peer) return;
                     auto pos = ask(_("New portal"), _("Position face,x,y:"), "0,0,0");
                     if (!pos) return;
                     int face = 0, x = 0, y = 0;
                     if (std::sscanf(pos->c_str(), "%d,%d,%d", &face, &x, &y) != 3) {
                         throw OpError(_("Use the format face,x,y."));
                     }
                     ops_.createPortal(*name, *peer, face, x, y);
                 }, false},
                {_("~O~pen"), setState("open")},
                {_("C~l~ose"), setState("closed")},
                {_("D~i~sable"), setState("disabled")},
                {_("~B~an"), [this, cache](long i) {
                     if (confirm(_("Ban this portal (and its peer)?"))) {
                         ops_.setPortalState(idOf(*cache, i), "banned");
                     }
                 }},
                {_("~D~elete"), [this, cache](long i) {
                     if (confirm(_("Cancel this portal?"))) {
                         ops_.deletePortal(idOf(*cache, i));
                     }
                 }},
            }));
    }

    void serverDialog() {
        showDialog(new ListDialog(
            _("Server"),
            [this] {
                const Row s = ops_.scheduledShutdown();
                return s.empty() ? std::string(_("No shutdown is scheduled."))
                                 : std::string(_("Shutdown in ")) + s[0] + " s" + (s[1].empty() ? "" : ": " + s[1]);
            },
            [] { return std::vector<std::string>{}; },
            {
                {_("~S~hut down..."), [this](long) {
                     auto delay = ask(_("Shut down server"), _("Delay in seconds (players see a notice):"), "60");
                     if (!delay) return;
                     auto message = ask(_("Shut down server"), _("Message for players (optional):"));
                     if (!message) return;
                     if (confirm(_("The server service will stop after the delay. Continue?"))) {
                         ops_.scheduleShutdown(std::atoi(delay->c_str()), *message);
                     }
                 }, false},
                {_("~C~ancel shutdown"), [this](long) { ops_.cancelShutdown(); }, false},
            }));
    }

    Ops &ops_;
};

int runApp(const std::string &databaseUrl) {
    try {
        Db db(databaseUrl);
        Ops ops(db);
        AdminApp app(ops);
        app.run();
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "ishtaria-admin: %s\n", e.what());
        return 1;
    }
}

} // namespace ishtariaadmin
