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
        RESET,
        HEX// 新增支持十六进制颜色
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

        static std::string getAnsiCode(const std::string &hexColor) {
            // 将十六进制颜色转换为ANSI转义序列
            if (hexColor.size() != 7 || hexColor[0] != '#') {
                throw std::invalid_argument("Invalid hex color format. Expected format: #RRGGBB");
            }
            int r = std::stoi(hexColor.substr(1, 2), nullptr, 16);
            int g = std::stoi(hexColor.substr(3, 2), nullptr, 16);
            int b = std::stoi(hexColor.substr(5, 2), nullptr, 16);
            return "\033[38;2;" + std::to_string(r) + ";" + std::to_string(g) + ";" + std::to_string(b) + "m";
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

            // 更新当前进度值（不超过总值）
            now_ = std::min(now, total_);
            if (force_complete) now_ = total_;

            // 计算经过的时间
            auto current_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double>(current_time - start_time_).count();

            // 计算百分比和已填充的进度条宽度
            int percent = calculatePercent(now_);
            int filled = calculateFilledWidth(now_);

            // 移动光标到指定行并清除当前行
            moveCursorToLine(line_index_);
            std::cout << "\r\033[2K";// 清除行并回到行首

            // 构建进度条字符串
            std::string bar = buildLabelString();                   // 标签部分
            bar += buildProgressBar(now_, filled, elapsed, percent);// 进度条主体

            // 计算剩余时间（用于 ETA 显示）
            double estimated_total = 0.0;
            double remaining = 0.0;

            if (now_ > 0) {
                estimated_total = elapsed * total_ / now_;// 总预计时间
                remaining = estimated_total - elapsed;    // 剩余时间
                if (remaining < 0) remaining = 0.0;       // 避免负数
            }

            // 构建时间信息（传递 elapsed, is_completed, remaining）
            bar += buildTimeInfoImpl(elapsed, (now_ == total_), remaining);

            // 刷新输出
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

        // 新增方法：设置时间颜色
        void setTimeColor(ColorType time_color) {
            time_color_code_ = ColorUtils::getAnsiCode(time_color);
        }

        // 新增方法：设置时间格式
        void setTimeFormat(const std::string &format) {
            time_format_ = format;
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


        // 新增成员变量：时间颜色代码
        std::string time_color_code_ = ColorUtils::getAnsiCode(ColorType::MAGENTA);

        // 新增成员变量：时间格式
        std::string time_format_;

        // 改进时间信息构建方法，使用经过的时间（elapsed time）进行格式化
        std::string buildTimeInfoImpl(double elapsed, bool is_completed, double remaining) const {
            std::ostringstream oss;
            std::string time_str;

            if (!time_format_.empty()) {
                std::string temp_format = time_format_;
                double time_source = is_completed ? elapsed : remaining;

                // 替换%S（整秒）
                auto pos = temp_format.find("%S");
                if (pos != std::string::npos) {
                    int seconds = static_cast<int>(time_source);
                    temp_format.replace(pos, 2, std::to_string(seconds));
                }

                // 替换%3N（毫秒，补零至3位）
                pos = temp_format.find("%3N");
                if (pos != std::string::npos) {
                    int milliseconds = static_cast<int>((time_source - static_cast<int>(time_source)) * 1000);
                    std::ostringstream oss_ms;
                    oss_ms << std::setw(3) << std::setfill('0') << milliseconds;
                    temp_format.replace(pos, 3, oss_ms.str());
                }

                // 拼接标签和单位
                if (is_completed) {
                    time_str = "Elapsed: " + temp_format + "s";
                } else {
                    time_str = "ETA: " + temp_format + "s";
                }
            } else {
                // 默认格式
                if (is_completed) {
                    time_str = " Elapsed: " + std::to_string(static_cast<int>(elapsed)) + "s";
                } else {
                    time_str = " ETA: " + std::to_string(static_cast<int>(remaining)) + "s";
                }
            }

            return time_color_code_ + time_str + reset_code_;
        }

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