#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define OS_WINDOWS
#else
#include <unistd.h>
#endif

namespace pulse {

    //todo :using a queue to manage.

    // 颜色枚举
    enum class ColorType {
        RED,
        GREEN,
        YELLOW,
        BLUE,
        MAGENTA,
        CYAN,
        WHITE,
        GRAY,
        BRIGHT_RED,
        BRIGHT_GREEN,
        BRIGHT_YELLOW,
        BRIGHT_BLUE,
        BRIGHT_MAGENTA,
        BRIGHT_CYAN,
        BRIGHT_WHITE,
        RESET
    };

    // 动画策略接口
    class AnimationStrategy {
    public:
        virtual ~AnimationStrategy() = default;
        virtual const char *getCurrentFrame(double elapsed_time) const = 0;
    };

    //默认动画
    class DefaultPulseAnimation : public AnimationStrategy {
    public:
        const char *getCurrentFrame(double elapsed_time) const override {
            static const char *pulses[] = {"▁", "▂", "▃", "▄", "▅", "▆", "▇", "█", "▇", "▆", "▅", "▄", "▃", "▂"};
            int pulse_idx = static_cast<int>(elapsed_time * 10) % 14;
            return pulses[pulse_idx];
        }
    };


    // 颜色工具类
    class ColorUtils {
    public:
        static std::string getAnsiCode(ColorType type) {
            static const std::unordered_map<ColorType, std::string> colorMap = {
                    {ColorType::RED, "\033[31m"},
                    {ColorType::GREEN, "\033[32m"},
                    {ColorType::YELLOW, "\033[33m"},
                    {ColorType::BLUE, "\033[34m"},
                    {ColorType::MAGENTA, "\033[35m"},
                    {ColorType::CYAN, "\033[36m"},
                    {ColorType::WHITE, "\033[37m"},
                    {ColorType::GRAY, "\033[90m"},
                    {ColorType::BRIGHT_RED, "\033[1;31m"},
                    {ColorType::BRIGHT_GREEN, "\033[1;32m"},
                    {ColorType::BRIGHT_YELLOW, "\033[1;33m"},
                    {ColorType::BRIGHT_BLUE, "\033[1;34m"},
                    {ColorType::BRIGHT_MAGENTA, "\033[1;35m"},
                    {ColorType::BRIGHT_CYAN, "\033[1;36m"},
                    {ColorType::BRIGHT_WHITE, "\033[1;37m"},
                    {ColorType::RESET, "\033[0m"}};
            return colorMap.at(type);
        }
    };

    // 进度条配置回调类型
    using BracketCallback = std::function<std::pair<std::string, std::string>(int percent)>;
    using ColorBlendCallback = std::function<ColorType(int position, int width, int percent)>;
    using TimeFormatCallback = std::function<std::string(double elapsed_time, bool is_completed)>;

    // 脉冲进度条类
    class PulseBar {
    public:
        explicit PulseBar(int total,
                          int width = 50,
                          const std::string &label = "Progress",
                          ColorType bar_color = ColorType::BRIGHT_CYAN,
                          ColorType label_color = ColorType::BRIGHT_WHITE,
                          AnimationStrategy *animation = new DefaultPulseAnimation())
            : total_(total),
              width_(width),
              label_(label),
              start_time_(std::chrono::high_resolution_clock::now()),
              animation_(animation) {
            // 初始化默认回调
            bracket_callback_ = [](int percent) {
                return std::make_pair(std::string("["), std::string("]"));
            };

            color_blend_callback_ = [bar_color](int pos, int width, int percent) {
                return bar_color;
            };

            // 设置颜色
            bar_color_code_ = ColorUtils::getAnsiCode(bar_color);
            label_color_code_ = ColorUtils::getAnsiCode(label_color);
            reset_code_ = ColorUtils::getAnsiCode(ColorType::RESET);

            enableAnsiTerminal();
            initializeLineIndex();
        }

        ~PulseBar() {
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            moveCursorToLine(line_index_);
            std::cout << "\033[2K";// 清除行
            if (line_index_ == next_line_index_ - 1) {
                std::cout << "\r";// 回到行首
            }
            std::cout.flush();
        }

        void update(int now, bool force_complete = false) {
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            now_ = std::min(now, total_);
            if (force_complete) now_ = total_;

            auto current_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(current_time - start_time_).count();
            int percent = calculatePercent(now_);
            int filled = calculateFilledWidth(now_);

            moveCursorToLine(line_index_);
            std::cout << "\r\033[2K";// 清除当前行

            std::string bar = buildLabelString();
            bar += buildProgressBar(now_, filled, elapsed, percent);
            bar += buildTimeInfo(now_, elapsed);

            std::cout << bar << std::flush;
        }

        void complete() {
            update(total_, true);
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            moveCursorToLine(line_index_);
            std::cout << std::endl;
        }

        void setLabel(const std::string &new_label) {
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            label_ = new_label;
            update(now_, false);
        }

        void setBracketCallback(BracketCallback callback) {
            bracket_callback_ = callback;
        }

        void setColorBlendCallback(ColorBlendCallback callback) {
            color_blend_callback_ = callback;
        }

        void setTimeFormatCallback(TimeFormatCallback callback) {
            time_format_callback_ = callback;
        }

        static void newline() {
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            std::cout << "\n";
            next_line_index_++;
        }

    private:
        int total_;
        int width_;
        std::string label_;
        std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
        int line_index_;
        int now_ = 0;
        AnimationStrategy *animation_;

        // 回调函数
        BracketCallback bracket_callback_;
        ColorBlendCallback color_blend_callback_;

        // 颜色代码
        std::string bar_color_code_;
        std::string label_color_code_;
        std::string reset_code_;

        // 静态成员
        static std::recursive_mutex global_mtx_;
        static std::atomic<int> next_line_index_;

        // 工具函数
        int calculatePercent(int now) const {
            return static_cast<int>((now * 100.0) / total_);
        }

        int calculateFilledWidth(int now) const {
            return static_cast<int>((now * width_) / total_);
        }

        std::string buildLabelString() const {
            return label_color_code_ + label_ + " " + reset_code_;
        }

        std::string buildProgressBar(int now, int filled, double elapsed, int percent) const {
            auto [left_bracket, right_bracket] = bracket_callback_(percent);

            std::string bar;
            bar += left_bracket;// 添加左括号

            for (int i = 0; i < width_; ++i) {
                if (i < filled) {
                    // 填充部分：应用颜色
                    bar += ColorUtils::getAnsiCode(color_blend_callback_(i, width_, percent)) + "█";
                } else if (i == filled && now < total_) {
                    // 动画部分：应用颜色+动画符号
                    bar += ColorUtils::getAnsiCode(color_blend_callback_(i, width_, percent)) +
                           animation_->getCurrentFrame(elapsed);
                } else {
                    // 空白部分
                    bar += ColorUtils::getAnsiCode(ColorType::RESET) + " ";
                }
            }

            bar += ColorUtils::getAnsiCode(ColorType::RESET);// 重置颜色
            bar += right_bracket;                            // 添加右括号

            // 百分比显示保持原逻辑
            bar += " " + ColorUtils::getAnsiCode(ColorType::BRIGHT_GREEN) +
                   std::to_string(percent) + "%" + ColorUtils::getAnsiCode(ColorType::RESET);

            return bar;
        }

        std::string buildTimeInfo(int now, double elapsed) const {
            if (now <= 0 || elapsed <= 0.1) return "";
            std::string time_info = " | " + ColorUtils::getAnsiCode(ColorType::MAGENTA);
            if (now == total_) {
                int minutes = static_cast<int>(elapsed) / 60;
                int seconds = static_cast<int>(elapsed) % 60;
                char buffer[16];
                snprintf(buffer, sizeof(buffer), "Elapsed: %02d:%02d", minutes, seconds);
                time_info += buffer;
            } else {
                double estimated_total = elapsed * total_ / now;
                double remaining = estimated_total - elapsed;
                int minutes = static_cast<int>(remaining) / 60;
                int seconds = static_cast<int>(remaining) % 60;
                char buffer[16];
                snprintf(buffer, sizeof(buffer), "ETA: %02d:%02d", minutes, seconds);
                time_info += buffer;
            }
            time_info += reset_code_;
            return time_info;
        }

        //todo: support format time
        std::string buildTimeInfoImpl(double elapsed, bool is_completed) const {
            std::ostringstream oss;
            if (is_completed) {
                int minutes = static_cast<int>(elapsed) / 60;
                int seconds = static_cast<int>(elapsed) % 60;
                int milliseconds = static_cast<int>((elapsed * 1000)) % 1000;
                oss << "Elapsed: " << std::setw(2) << std::setfill('0') << minutes << ":"
                    << std::setw(2) << seconds << "."
                    << std::setw(3) << milliseconds;
            } else {
                double estimated_total = elapsed * total_ / now_;
                double remaining = estimated_total - elapsed;
                int minutes = static_cast<int>(remaining) / 60;
                int seconds = static_cast<int>(remaining) % 60;
                int milliseconds = static_cast<int>((remaining * 1000)) % 1000;
                oss << "ETA: " << std::setw(2) << std::setfill('0') << minutes << ":"
                    << std::setw(2) << seconds << "."
                    << std::setw(3) << milliseconds;
            }
            return "\033[35m" + oss.str() + "\033[0m";
        }

        TimeFormatCallback time_format_callback_ = [this](double elapsed, bool is_completed) {
            return buildTimeInfoImpl(elapsed, is_completed);
        };


        void moveCursorToLine(int target_line) {
            if (target_line > line_index_) {
                std::cout << "\033[" << (target_line - line_index_) << "B";
            } else if (target_line < line_index_) {
                std::cout << "\033[" << (line_index_ - target_line) << "A";
            }
            line_index_ = target_line;
            std::cout << "\r";
        }

        void enableAnsiTerminal() {
#ifdef OS_WINDOWS
            HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            GetConsoleMode(hOut, &mode);
            mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
            SetConsoleMode(hOut, mode);
#endif
        }

        void initializeLineIndex() {
            std::lock_guard<std::recursive_mutex> lock(global_mtx_);
            static int global_next_line_index = 0;
            line_index_ = global_next_line_index++;
            if (line_index_ > 0) {
                std::cout << "\n";
            }
        }
    };

    // 初始化静态成员
    inline std::recursive_mutex PulseBar::global_mtx_;
    inline std::atomic<int> PulseBar::next_line_index_(0);

}// namespace pulse