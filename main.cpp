#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <cctype>
#include <chrono>
#include <ctime>
#include <algorithm>
#include <fstream>
#include <thread>
#include <random>
#include <cstdlib>
#include <csignal>
#include <sys/ioctl.h>
#include <unistd.h>

struct Metric {
    std::string name;
    double current;
    double target;
    double weight;
};

static std::mt19937 g_rng(std::random_device{}());

// ---------------------------------------------------------------------------
// Terminal / lifecycle helpers
// ---------------------------------------------------------------------------

void signalHandler(int signum) {
    const char msg[] = "\033[?25h\033[0m\n";
    if (::write(STDOUT_FILENO, msg, sizeof(msg) - 1) < 0) {
        // Ignore write failures inside a signal handler
    }
    ::_exit(signum);
}

std::string getHomeDir() {
    const char* home = std::getenv("HOME");
    return home ? std::string(home) : ".";
}

std::string getMetricsPath() { return getHomeDir() + "/.divergence_metrics.csv"; }
std::string getLogPath()     { return getHomeDir() + "/.divergence_history.log"; }

std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&t_now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    return std::string(buf);
}

void sleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

void printHelp() {
    std::cout << "\033[1;36m  === FUTURE GADGET LAB: FG-204 ===\033[0m\n\n"
              << "USAGE:\n"
              << "  divergence [COMMAND] [ARGS...]\n\n"
              << "COMMANDS:\n"
              << "  (none)            Boot animated dashboard\n"
              << "  -f, --fast        Display dashboard instantly (no animation)\n"
              << "  -u, --update      Open interactive metric update mode\n"
              << "  -e, edit          Open the configuration file to change subjects/limits\n"
              << "  add <idx> <val>   Add <val> to metric <idx> (1-based index). Alternately use '+'\n"
              << "                    Example: divergence add 1 2.5\n"
              << "  -l, log           View recent worldline shifts history\n"
              << "  -h, --help, help  Show this help message\n";
}

// ---------------------------------------------------------------------------
// Config & State Persistence
// ---------------------------------------------------------------------------

void loadMetrics(std::vector<Metric>& metrics) {
    std::ifstream file(getMetricsPath());
    if (!file.is_open()) return;
    
    metrics.clear();
    std::string line;
    
    auto trim = [](std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { s = ""; return; }
        size_t end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
    };

    while (std::getline(file, line)) {
        trim(line);
        if (line.empty() || line[0] == '#') continue;
        
        std::stringstream ss(line);
        std::string name, cur_str, tgt_str, wt_str;
        
        if (std::getline(ss, name, ',') &&
            std::getline(ss, cur_str, ',') &&
            std::getline(ss, tgt_str, ',') &&
            std::getline(ss, wt_str, ',')) {
            
            trim(name); trim(cur_str); trim(tgt_str); trim(wt_str);
            try {
                metrics.push_back({
                    name, 
                    std::max(0.0, std::stod(cur_str)), 
                    std::max(0.1, std::stod(tgt_str)), // prevent div by zero
                    std::stod(wt_str)
                });
            } catch (...) {
                // Skip invalid lines
            }
        }
    }
}

void saveMetrics(const std::vector<Metric>& metrics) {
    std::string path = getMetricsPath();
    std::string tmpPath = path + ".tmp";
    std::ofstream file(tmpPath, std::ios::trunc);
    if (!file.is_open()) return;
    
    file << "# FUTURE GADGET LAB METRICS CONFIGURATION\n";
    file << "# Format: Subject Name, Current Progress, Target Goal, Weight (Decimal)\n";
    file << "# (Note: Weights should ideally add up to 1.00 total)\n";
    
    for (const auto& m : metrics) {
        file << m.name << ", " 
             << std::fixed << std::setprecision(2) << m.current << ", " 
             << m.target << ", " 
             << m.weight << "\n";
    }
    file.close();
    std::rename(tmpPath.c_str(), path.c_str());
}

double calculateDivergence(const std::vector<Metric>& metrics, bool& all_met) {
    double div = 0.0;
    all_met = true;
    for (const auto& m : metrics) {
        double ratio = std::min(1.0, m.current / m.target);
        div += ratio * m.weight;
        if (m.current < m.target) all_met = false;
    }
    return div;
}

void logShift(const std::string& note, double new_div) {
    std::ofstream log(getLogPath(), std::ios::app);
    if (!log.is_open()) return;
    log << "[" << getTimestamp() << "] "
        << "DIV: " << std::fixed << std::setprecision(6) << new_div << "% | "
        << note << "\n";
}

int getDaysUntilApril30() {
    auto now = std::chrono::system_clock::now();
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now = *std::localtime(&t_now);

    std::tm tm_target = {};
    tm_target.tm_year = tm_now.tm_year;
    tm_target.tm_mon = 3; 
    tm_target.tm_mday = 30;
    tm_target.tm_hour = 23;
    tm_target.tm_min = 59;
    tm_target.tm_sec = 59;
    tm_target.tm_isdst = -1; 

    std::time_t t_target = std::mktime(&tm_target);
    if (t_target < t_now) {
        tm_target.tm_year += 1;
        tm_target.tm_isdst = -1; 
        t_target = std::mktime(&tm_target);
    }
    double diff = std::difftime(t_target, t_now);
    return std::max(1, static_cast<int>(diff / (60 * 60 * 24)));
}

// ---------------------------------------------------------------------------
// Low-level visual fx
// ---------------------------------------------------------------------------

void typeOut(const std::string& s, int delayMs = 12, const std::string& colorCode = "\033[38;5;242m") {
    std::cout << colorCode;
    for (char c : s) {
        std::cout << c;
        std::cout.flush();
        sleepMs(delayMs);
    }
    std::cout << "\033[0m\n";
}

void spinFor(int totalMs, const std::string& label) {
    static const char* frames[] = {"⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏"};
    int elapsed = 0;
    int i = 0;
    const int step = 60;
    while (elapsed < totalMs) {
        std::cout << "\r   \033[38;5;80m" << frames[i % 10] << "\033[0m  \033[38;5;244m"
                   << label << "\033[0m   " << std::flush;
        sleepMs(step);
        elapsed += step;
        ++i;
    }
    std::cout << "\r" << std::string(label.size() + 10, ' ') << "\r";
}

void scanlineFlicker(int width) {
    std::uniform_int_distribution<int> pick(0, 100);
    if (pick(g_rng) < 35) {
        std::cout << "\033[38;5;236m" << std::string(width, '-') << "\033[0m\n";
    }
}

int terminalWidthGuess() { return 38; } 

void chirp() { std::cout << "\a"; std::cout.flush(); }

// ---------------------------------------------------------------------------
// Screen centering
// ---------------------------------------------------------------------------

int detectTerminalColumns() {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) {
        return w.ws_col;
    }
    if (const char* cols_env = std::getenv("COLUMNS")) {
        int cols = std::atoi(cols_env);
        if (cols > 0) return cols;
    }
    return terminalWidthGuess();
}

class MarginStreambuf : public std::streambuf {
public:
    MarginStreambuf(std::streambuf* dest, int margin)
        : dest_(dest), margin_(margin), need_margin_(true), esc_state_(ST_NORMAL) {}
protected:
    int overflow(int c) override {
        if (c == traits_type::eof()) return traits_type::not_eof(c);
        char ch = static_cast<char>(c);

        if (esc_state_ == ST_SAW_ESC) {
            esc_state_ = (ch == '[') ? ST_IN_CSI : ST_NORMAL;
            return dest_->sputc(ch);
        }
        if (esc_state_ == ST_IN_CSI) {
            if (ch >= '@' && ch <= '~') esc_state_ = ST_NORMAL;
            return dest_->sputc(ch);
        }
        if (ch == '\033') { 
            esc_state_ = ST_SAW_ESC;
            return dest_->sputc(ch);
        }
        if (ch == '\n' || ch == '\r') {
            need_margin_ = true;
            return dest_->sputc(ch);
        }

        if (need_margin_ && margin_ > 0) {
            for (int i = 0; i < margin_; ++i) dest_->sputc(' ');
        }
        need_margin_ = false;
        return dest_->sputc(ch);
    }
private:
    enum EscState { ST_NORMAL, ST_SAW_ESC, ST_IN_CSI };
    std::streambuf* dest_;
    int margin_;
    bool need_margin_;
    EscState esc_state_;
};

int gradientColor(double t) {
    t = std::min(1.0, std::max(0.0, t));
    if (t < 0.33) {
        double s = t / 0.33;
        return 196 - static_cast<int>(s * (196 - 202)); 
    } else if (t < 0.66) {
        double s = (t - 0.33) / 0.33;
        return 202 - static_cast<int>(s * (202 - 220)); 
    } else {
        double s = (t - 0.66) / 0.34;
        return 220 - static_cast<int>(s * (220 - 46));  
    }
}

std::string colorCode(int c) { return "\033[38;5;" + std::to_string(c) + "m"; }

std::string statusLabel(double divergence) {
    if (divergence >= 0.999) return "STEINS GATE LOCKED";
    if (divergence < 0.5) return "ALPHA - HIGH RESISTANCE";
    return "BETA - CONVERGING";
}

std::string scrambleDigits(const std::string& target, int frame, int base_lock_frame, int stagger_per_char) {
    static std::uniform_int_distribution<int> dist(0, 9);
    std::string out = target;
    for (size_t i = 0; i < out.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(out[i]))) continue;
        int lock_frame = base_lock_frame + static_cast<int>(i) * stagger_per_char;
        if (frame < lock_frame) out[i] = static_cast<char>('0' + dist(g_rng));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Welcome / AMADEUS intro sequence
// ---------------------------------------------------------------------------
// A short intro that plays before the dashboard boots: a greeting, a small
// stylized (original, non-reproduced) console-art face labeled AMADEUS, and
// a loading bar that is literally the label text filling in as it "loads",
// with occasional random glitch corruption for flavor.

std::string glitchText(const std::string& s, double glitch_chance) {
    static const std::string glitch_chars = "@#$%&*!?/\\|<>^~=+";
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(glitch_chars.size()) - 1);
    std::string out = s;
    for (auto& c : out) {
        if (c != ' ' && chance(g_rng) < glitch_chance) {
            c = glitch_chars[pick(g_rng)];
        }
    }
    return out;
}

void printGlitchLine(const std::string& s, const std::string& colorCode, double glitch_chance) {
    std::cout << colorCode << glitchText(s, glitch_chance) << "\033[0m\n";
}

// A "loading bar" that is the label text itself, filling in character by
// character with occasional glitch corruption on unfilled frames.
void animateTextFillBar(const std::string& label, int totalMs) {
    int len = static_cast<int>(label.size());
    int steps = 44;
    int step_delay = std::max(8, totalMs / steps);
    std::uniform_real_distribution<double> chance(0.0, 1.0);
    static const std::string glitch_chars = "@#$%&*!?/\\|<>^~=+";
    std::uniform_int_distribution<int> pick(0, static_cast<int>(glitch_chars.size()) - 1);

    for (int step = 0; step <= steps; ++step) {
        double frac = static_cast<double>(step) / steps;
        int fill_chars = static_cast<int>(frac * len);
        bool glitch_frame = chance(g_rng) < 0.14;

        std::cout << "\r  ";
        for (int i = 0; i < len; ++i) {
            char c = label[i];
            if (glitch_frame && i >= fill_chars && c != ' ' && chance(g_rng) < 0.20) {
                c = glitch_chars[pick(g_rng)];
            }
            if (i < fill_chars) {
                std::cout << "\033[1;48;5;205;38;5;16m" << c << "\033[0m";
            } else {
                std::cout << "\033[38;5;238m" << c << "\033[0m";
            }
        }
        std::cout << "  " << std::setw(3) << static_cast<int>(frac * 100) << "%" << std::flush;
        sleepMs(step_delay);
    }
    std::cout << "\n";
}

void playWelcomeSequence() {
    std::cout << "\033[?25l\033[2J\033[H\n";
    std::uniform_real_distribution<double> chance(0.0, 1.0);

    if (chance(g_rng) < 0.3) {
        printGlitchLine("  Welcome back, Souvik", "\033[1;38;5;213m", 0.18);
    } else {
        std::cout << "\033[1;38;5;213m  Welcome back, Souvik\033[0m\n";
    }
    std::cout << "\033[38;5;244m  // AMADEUS SYSTEM // KURISU INTERFACE\033[0m\n\n";

    scanlineFlicker(terminalWidthGuess());
    std::cout << "\n";

    animateTextFillBar("INITIALIZING AMADEUS SYSTEM...", 1500);
    sleepMs(150);
    scanlineFlicker(terminalWidthGuess());

    if (chance(g_rng) < 0.25) {
        printGlitchLine("  >> Amadeus online.", "\033[38;5;80m", 0.2);
    } else {
        typeOut("  >> Amadeus online.", 14, "\033[38;5;80m");
    }
    sleepMs(350);
}

// ---------------------------------------------------------------------------
// Boot sequence
// ---------------------------------------------------------------------------

void playBootSequence() {
    std::cout << "\033[?25l\033[2J\033[H";

    std::vector<std::string> lines = {
        "  >> FG-204 METER // PROTOTYPE v3.0",
        "  >> INIT REV. 7 HARDWARE BRIDGE...",
        "  >> CALIBRATING STEINER INTERFACE.",
        "  >> SYNCING D-MAIL BUFFER.........",
        "  >> READING ATTRACTOR SENSORS.....",
        "  >> LOCKING OBSERVER WORLDLINE"
    };

    for (const auto& l : lines) {
        typeOut(l, 10);
        scanlineFlicker(terminalWidthGuess());
    }

    spinFor(650, "CONVERGING PROBABILITY");
    std::cout << " \033[1;32m[OK]\033[0m\n";
    sleepMs(120);
}

// ---------------------------------------------------------------------------
// Nixie tube rendering
// ---------------------------------------------------------------------------

void renderNixie(const std::string& val_str, bool glow, bool dim_flicker, double divergence, const std::string& statusText) {
    int grad = gradientColor(divergence);
    std::string frame_color = dim_flicker ? "\033[38;5;94m" : colorCode(grad);
    int inner_width = static_cast<int>(val_str.size()) * 4 + 1; 

    std::string label = " DIVERGENCE READING ";
    int pad = std::max(0, inner_width - static_cast<int>(label.size()) - 2);
    int pad_l = pad / 2, pad_r = pad - pad_l;
    std::cout << "  " << frame_color << "╭" << std::string(pad_l, '-')
               << "\033[1;38;5;117m" << label << "\033[0m" << frame_color
               << std::string(pad_r, '-') << "╮\033[0m\n";

    std::cout << "  " << frame_color << "+---+---+---+---+---+---+---+---+\033[0m\n";
    std::cout << "  " << frame_color << "|\033[0m";
    for (char c : val_str) {
        if (c == '.') {
            std::cout << "\033[1;38;5;214m . \033[0m" << frame_color << "|\033[0m";
        } else if (glow) {
            std::cout << "\033[1;38;5;220m " << c << " \033[0m" << frame_color << "|\033[0m";
        } else {
            std::cout << "\033[38;5;166m " << c << " \033[0m" << frame_color << "|\033[0m";
        }
    }
    std::cout << "\n";
    std::cout << "  " << frame_color << "+---+---+---+---+---+---+---+---+\033[0m\n";
    std::cout << "  " << frame_color << "╰" << std::string(std::max(0, inner_width - 2), '-') << "╯\033[0m\n";

    if (!statusText.empty()) {
        const std::string status_prefix = "STATUS: ";
        int content_len = static_cast<int>(status_prefix.size()) + static_cast<int>(statusText.size());
        int text_pad = std::max(0, (inner_width - content_len) / 2);
        std::cout << std::string(text_pad + 2, ' ') << "\033[38;5;244m" << status_prefix << "\033[0m"
                   << colorCode(grad) << statusText << "\033[0m\n";
    }
}

void renderProgressBar(double divergence, int width = 18) {
    static const char* eighths[] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    double frac = std::min(1.0, std::max(0.0, divergence));
    double exact_filled = frac * width;
    int full_cells = static_cast<int>(exact_filled);
    int remainder_eighths = static_cast<int>(std::round((exact_filled - full_cells) * 8));

    std::cout << "  Progress: [";
    for (int b = 0; b < width; ++b) {
        if (b < full_cells) {
            double t = static_cast<double>(b) / width;
            int color = t < 0.5 ? 196 - static_cast<int>(t * 2 * (196 - 220))
                                : 220 - static_cast<int>((t - 0.5) * 2 * (220 - 46));
            std::cout << "\033[38;5;" << color << "m█\033[0m";
        } else if (b == full_cells && remainder_eighths > 0) {
            std::cout << "\033[38;5;46m" << eighths[remainder_eighths] << "\033[0m";
        } else {
            std::cout << "\033[38;5;238m·\033[0m";
        }
    }
    std::cout << "] " << std::setw(5) << std::fixed << std::setprecision(1)
               << std::min(100.0, divergence * 100) << "%\n";
}

void animateNixieTubes(double divergence) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6) << divergence;
    std::string target = ss.str();

    std::uniform_int_distribution<int> dist(0, 9);
    std::uniform_int_distribution<int> jitter(0, 100);

    const int total_frames = 42;

    for (int frame = 0; frame < total_frames; ++frame) {
        std::cout << "\r\033[H\033[2J";
        std::cout << "\033[1;36m  === FUTURE GADGET LAB: FG-204 ===\033[0m\n";
        std::cout << "\033[38;5;244m      divergence meter // rev.7\033[0m\n\n";

        std::string frame_str = target;
        int locked_count = 0;
        for (size_t i = 0; i < target.size(); ++i) {
            int lock_frame = 10 + static_cast<int>(i) * 4;
            if (target[i] != '.' && frame < lock_frame) {
                frame_str[i] = '0' + dist(g_rng);
            } else if (target[i] != '.') {
                ++locked_count;
            }
        }

        bool dim_flicker = jitter(g_rng) < 8; 
        bool show_status = frame >= total_frames - 6;
        renderNixie(frame_str, show_status, dim_flicker, divergence,
                     show_status ? statusLabel(divergence) : "");

        double reveal_div = divergence * (static_cast<double>(frame) / total_frames);
        renderProgressBar(std::max(reveal_div, divergence * 0.05 * (frame > 2)));

        scanlineFlicker(terminalWidthGuess());

        std::cout.flush();
        int frame_delay = 55 - static_cast<int>((static_cast<double>(frame) / total_frames) * 30);
        sleepMs(std::max(18, frame_delay)); 
    }

    chirp();
    std::cout << "\033[?25h";
}

void renderMiniBar(double reveal_pct, int color, int mini_width) {
    static const char* eighths[] = {" ", "▏", "▎", "▍", "▌", "▋", "▊", "▉", "█"};
    double exact = reveal_pct * mini_width;
    int full = static_cast<int>(exact);
    int rem = static_cast<int>(std::round((exact - full) * 8));

    std::cout << colorCode(color);
    for (int b = 0; b < mini_width; ++b) {
        if (b < full) std::cout << "█";
        else if (b == full && rem > 0) std::cout << eighths[rem];
        else std::cout << "\033[38;5;238m·" << colorCode(color);
    }
    std::cout << "\033[0m";
}

void animateBreakdownSection(const std::vector<Metric>& metrics, int days_left, double divergence) {
    std::ostringstream nss;
    nss << std::fixed << std::setprecision(6) << divergence;
    std::string nixie_val = nss.str();
    std::string status_text = statusLabel(divergence);

    std::string days_target = std::to_string(days_left);
    int deadline_color = days_left > 60 ? 46 : (days_left > 21 ? 220 : 196);

    struct RowTarget {
        std::string cur_str;
        std::string rate_str;
        double pct;
        bool ok;
        double per_day;
    };
    std::vector<RowTarget> rows;
    rows.reserve(metrics.size());
    for (const auto& m : metrics) {
        double remaining = std::max(0.0, m.target - m.current);
        double per_day = remaining / days_left;
        double pct = std::min(1.0, m.current / m.target);
        std::ostringstream cs;
        cs << static_cast<int>(m.current) << "/" << static_cast<int>(m.target);
        std::ostringstream rs;
        rs << std::fixed << std::setprecision(2) << per_day;
        rows.push_back({cs.str(), rs.str(), pct, remaining <= 0.0, per_day});
    }

    const std::string rule = "\033[38;5;94m--------------------------------------\033[0m";
    const int mini_width = 5; 

    const int deadline_lock = 12;
    const int row_stagger = 6;
    const int row_scramble_len = 9;
    const int total_frames = deadline_lock + row_stagger * static_cast<int>(metrics.size()) + row_scramble_len + 5;

    std::uniform_int_distribution<int> jitter(0, 100);

    for (int frame = 0; frame < total_frames; ++frame) {
        std::cout << "\r\033[H\033[2J";
        std::cout << "\033[1;36m  === FUTURE GADGET LAB: FG-204 ===\033[0m\n";
        std::cout << "\033[38;5;244m      divergence meter // rev.7\033[0m\n\n";

        bool dim_flicker = jitter(g_rng) < 5;
        renderNixie(nixie_val, true, dim_flicker, divergence, status_text);
        renderProgressBar(divergence);

        std::cout << "\n" << rule << "\n";

        bool deadline_locked = frame >= deadline_lock;
        std::string days_show = deadline_locked ? days_target
                                                  : scrambleDigits(days_target, frame, deadline_lock, 0);
        std::cout << "  \033[38;5;244m>> \033[0mFIELD    : "
                   << (divergence < 0.5 ? "\033[1;31mAlpha (High Resistance)\033[0m"
                                        : "\033[1;33mBeta (Converging)\033[0m") << "\n";
        std::cout << "  \033[38;5;244m>> \033[0mDEADLINE : "
                   << colorCode(deadline_locked ? deadline_color : 240)
                   << "\033[1m" << days_show << " days\033[0m\033[38;5;244m until April 30\033[0m\n";
        std::cout << rule << "\n";
        std::cout << "  \033[1;38;5;117mMETRIC BREAKDOWN\033[0m \033[38;5;244m(burn/day)\033[0m\n";

        for (size_t i = 0; i < metrics.size(); ++i) {
            const auto& m = metrics[i];
            const auto& rt = rows[i];
            int row_start = deadline_lock + static_cast<int>(i) * row_stagger;
            int row_lock = row_start + row_scramble_len;

            if (frame < row_start) {
                std::cout << "  \033[38;5;236m[" << (i + 1) << "] " << std::left << std::setw(10) << m.name << " ...scanning\033[0m\n";
                continue;
            }

            bool row_locked = frame >= row_lock;
            std::string cur_show = row_locked ? rt.cur_str : scrambleDigits(rt.cur_str, frame, row_start, 0);
            std::string rate_show = row_locked ? rt.rate_str : scrambleDigits(rt.rate_str, frame, row_start, 0);

            double sub_progress = std::min(1.0, static_cast<double>(frame - row_start) / row_scramble_len);
            double reveal_pct = row_locked ? rt.pct : rt.pct * sub_progress;
            int pct_color = row_locked ? gradientColor(rt.pct) : 240;

            std::cout << "  \033[38;5;67m[" << (i + 1) << "]\033[0m "
                       << "\033[38;5;117m" << std::left << std::setw(10) << m.name << "\033[0m"
                       << ": " << colorCode(pct_color) << std::setw(5) << std::left << cur_show << "\033[0m ";
            renderMiniBar(reveal_pct, pct_color, mini_width);

            if (row_locked) {
                if (!rt.ok) {
                    int rate_color = rt.per_day > 1.0 ? 196 : (rt.per_day > 0.3 ? 220 : 46);
                    std::cout << " \033[38;5;244m(\033[0m" << colorCode(rate_color) << rate_show
                               << "/d\033[0m\033[38;5;244m)\033[0m\n";
                } else {
                    std::cout << " \033[38;5;244m(\033[0m\033[1;32mOK\033[0m\033[38;5;244m)\033[0m\n";
                }
            } else {
                std::cout << " \033[38;5;244m(\033[0m\033[38;5;240m" << rate_show
                           << "/d\033[0m\033[38;5;244m)\033[0m\n";
            }
        }
        std::cout << rule << "\n";

        scanlineFlicker(terminalWidthGuess());
        std::cout.flush();

        int frame_delay = 42 - static_cast<int>((static_cast<double>(frame) / total_frames) * 24);
        sleepMs(std::max(16, frame_delay));
    }

    chirp();
}

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------

void displayDashboard(const std::vector<Metric>& metrics, bool animated) {
    bool all_met = false;
    double raw_div = calculateDivergence(metrics, all_met);
    double display_div = all_met ? 1.048596 : raw_div;
    int days_left = getDaysUntilApril30();

    if (animated) {
        playWelcomeSequence();
        playBootSequence();
        animateNixieTubes(display_div);
    } else {
        std::cout << "\033[2J\033[H\033[1;36m  === FUTURE GADGET LAB: FG-204 ===\033[0m\n\n";
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6) << display_div;
        renderNixie(ss.str(), true, false, display_div, statusLabel(display_div));
        renderProgressBar(display_div);
    }

    std::string rule = "\033[38;5;94m--------------------------------------\033[0m";

    if (all_met) {
        std::cout << "\n" << rule << "\n";
        typeOut("  >> ATTRACTOR FIELD : STEINS GATE", 8, "\033[1;32m");
        typeOut("  >> TARGET REACHED  : 1.048596%", 8, "\033[1;32m");
        typeOut("  >> ALL CRITERIA SATISFIED.", 8, "\033[1;32m");
        chirp();
        std::cout << rule << "\n";
        return;
    }

    if (animated) {
        animateBreakdownSection(metrics, days_left, display_div);
        return;
    }

    std::cout << "\n" << rule << "\n";
    int deadline_color = days_left > 60 ? 46 : (days_left > 21 ? 220 : 196);
    std::cout << "  \033[38;5;244m>> \033[0mFIELD    : "
               << (raw_div < 0.5 ? "\033[1;31mAlpha (High Resistance)\033[0m"
                                 : "\033[1;33mBeta (Converging)\033[0m") << "\n";
    std::cout << "  \033[38;5;244m>> \033[0mDEADLINE : " << colorCode(deadline_color)
               << "\033[1m" << days_left << " days\033[0m\033[38;5;244m until April 30\033[0m\n";
    std::cout << rule << "\n";
    std::cout << "  \033[1;38;5;117mMETRIC BREAKDOWN\033[0m \033[38;5;244m(burn/day)\033[0m\n";

    const int mini_width = 5; 

    for (size_t i = 0; i < metrics.size(); ++i) {
        const auto& m = metrics[i];
        double remaining = std::max(0.0, m.target - m.current);
        double per_day = remaining / days_left;
        double pct = std::min(1.0, m.current / m.target);
        int pct_color = gradientColor(pct);

        std::ostringstream frac;
        frac << static_cast<int>(m.current) << "/" << static_cast<int>(m.target);

        std::cout << "  \033[38;5;67m[" << (i + 1) << "]\033[0m "
                   << "\033[38;5;117m" << std::left << std::setw(10) << m.name << "\033[0m"
                   << ": " << colorCode(pct_color) << std::setw(5) << std::left << frac.str() << "\033[0m ";
        renderMiniBar(pct, pct_color, mini_width);

        if (remaining > 0) {
            int rate_color = per_day > 1.0 ? 196 : (per_day > 0.3 ? 220 : 46);
            std::cout << " \033[38;5;244m(\033[0m" << colorCode(rate_color) << std::fixed
                       << std::setprecision(2) << per_day << "/d\033[0m\033[38;5;244m)\033[0m\n";
        } else {
            std::cout << " \033[38;5;244m(\033[0m\033[1;32mOK\033[0m\033[38;5;244m)\033[0m\n";
        }
    }
    std::cout << rule << "\n";
}

// ---------------------------------------------------------------------------
// Interactive update mode
// ---------------------------------------------------------------------------

void runInteractiveUpdate(std::vector<Metric>& metrics) {
    while (true) {
        std::cout << "\n  Select metric (1-" << metrics.size() << "), [H]istory, or 0 to exit: ";
        std::string input;
        if (!(std::cin >> input) || input == "0") break;

        if (input == "h" || input == "H") {
            std::cout << "\n  --- RECENT WORLDLINE SHIFTS ---\n";
            std::ifstream log(getLogPath());
            if (log.is_open()) {
                std::string line;
                std::vector<std::string> lines;
                while (std::getline(log, line)) lines.push_back(line);
                int start = std::max(0, static_cast<int>(lines.size()) - 5);
                for (size_t i = start; i < lines.size(); ++i) {
                    std::cout << "  " << lines[i] << "\n";
                }
            } else {
                std::cout << "  No log history recorded yet.\n";
            }
            std::cout << "  -------------------------------\n";
            continue;
        }

        try {
            int choice = std::stoi(input);
            if (choice >= 1 && choice <= static_cast<int>(metrics.size())) {
                int idx = choice - 1;
                std::cout << "  Enter new total for " << metrics[idx].name << " (Current: " << metrics[idx].current << "): ";
                double val;
                if (std::cin >> val && val >= 0) {
                    double old_val = metrics[idx].current;
                    metrics[idx].current = val;
                    saveMetrics(metrics);

                    bool dummy;
                    double new_div = calculateDivergence(metrics, dummy);
                    std::ostringstream ss;
                    ss << metrics[idx].name << " updated: " << old_val << " -> " << val;
                    logShift(ss.str(), new_div);

                    spinFor(300, "COMMITTING SHIFT");
                    std::cout << "  \033[32m[Shift Recorded to Worldline]\033[0m\n";
                    chirp();
                } else {
                    std::cin.clear();
                    std::cin.ignore(10000, '\n');
                    std::cout << "  \033[31mInvalid input.\033[0m\n";
                }
            } else {
                std::cout << "  \033[31mInvalid selection.\033[0m\n";
            }
        } catch (...) {
            std::cout << "  \033[31mInvalid selection.\033[0m\n";
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    int content_width = terminalWidthGuess();
    int margin = std::max(0, (detectTerminalColumns() - content_width) / 2);
    static MarginStreambuf marginBuf(std::cout.rdbuf(), margin);
    std::cout.rdbuf(&marginBuf);

    std::vector<Metric> metrics;
    loadMetrics(metrics);

    // If the file didn't exist or was empty, populate with default subjects
    if (metrics.empty()) {
        metrics = {
            {"Maths",      0.0, 11.0, 0.15},
            {"Physics",    0.0, 15.0, 0.15},
            {"Chemistry",  0.0,  8.0, 0.10},
            {"FEEE",       0.0,  8.0, 0.15},
            {"JELET PYQ",  0.0,  5.0, 0.12},
            {"WBJEE PYQ",  0.0, 10.0, 0.13},
            {"Mock Tests", 0.0, 50.0, 0.20}
        };
        saveMetrics(metrics);
    }

    if (argc > 1) {
        std::string arg1 = argv[1];

        if (arg1 == "-h" || arg1 == "--help" || arg1 == "help") {
            printHelp();
            return 0;
        }

        // Open config file in text editor
        if (arg1 == "-e" || arg1 == "edit") {
            std::string editor = "nano"; // Fallback editor
            if (const char* env_e = std::getenv("EDITOR")) {
                editor = env_e;
            }
            std::string cmd = editor + " " + getMetricsPath();
            int sys_ret = system(cmd.c_str());
            (void)sys_ret; // suppress unused warning
            return 0;
        }

        if (arg1 == "-f" || arg1 == "--fast") {
            displayDashboard(metrics, false);
            return 0;
        }

        if (arg1 == "-u" || arg1 == "u" || arg1 == "--update") {
            displayDashboard(metrics, false);
            runInteractiveUpdate(metrics);
            return 0;
        }

        if ((arg1 == "+" || arg1 == "add") && argc == 4) {
            try {
                int idx = std::stoi(argv[2]) - 1;
                double delta = std::stod(argv[3]);
                if (idx >= 0 && idx < static_cast<int>(metrics.size()) && delta != 0) {
                    metrics[idx].current = std::max(0.0, metrics[idx].current + delta);
                    saveMetrics(metrics);

                    bool dummy;
                    double new_div = calculateDivergence(metrics, dummy);
                    std::ostringstream ss;
                    ss << metrics[idx].name << " adjusted by " << (delta > 0 ? "+" : "") << delta
                       << " (New Total: " << metrics[idx].current << ")";
                    logShift(ss.str(), new_div);

                    std::cout << "  \033[32m[+] Worldline Updated: " << ss.str()
                               << " | Divergence: " << std::fixed << std::setprecision(6) << new_div << "%\033[0m\n";
                    return 0;
                } else {
                    std::cerr << "  Invalid index or delta value.\n";
                    return 1;
                }
            } catch (...) {
                std::cerr << "  Invalid parameters for delta update.\n";
                return 1;
            }
        }

        if (arg1 == "log" || arg1 == "-l") {
            std::ifstream log(getLogPath());
            std::string line;
            std::cout << "  \033[1;36m=== WORLDLINE SHIFT LOG ===\033[0m\n";
            while (std::getline(log, line)) {
                std::cout << "  " << line << "\n";
            }
            return 0;
        }
    }

    displayDashboard(metrics, true);

    std::cout << "\n  \033[1;37m[U]\033[0m Update  |  \033[1;37m[Enter]\033[0m Continue: ";
    
    std::cin.clear();
    std::string choice;
    std::getline(std::cin, choice);

    if (choice == "u" || choice == "U") {
        runInteractiveUpdate(metrics);
    }

    return 0;
}
