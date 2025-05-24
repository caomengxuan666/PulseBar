#include "PulseBar.hpp"
#include <iostream>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

// 自定义动画策略
class RainbowAnimation : public pulse::AnimationStrategy {
public:
    // ✅ 正确覆盖基类虚函数（添加percent参数）
    const char *getCurrentFrame(double elapsed_time, int percent) const override {
        static const char *frames[] = {"🌈", "ROYGBIV", "🌟", "✨", "⚡"};
        // 使用percent参数增加动画变化
        return frames[static_cast<int>((elapsed_time * 2) + (percent / 20.0)) % 5];
    }
};

// 示例1: 基本用法
void example_basic() {
    pulse::PulseBar bar1("下载中");
    //更换默认动画策略为实心块
    bar1.setAnimation(&pulse::solidBlockAnimation);
    for (int i = 0; i <= 100; i += 2) {
        bar1.update(i);
        std::this_thread::sleep_for(50ms);
    }
    bar1.complete();
    pulse::PulseBar::newline();
}

// 示例2: 自定义动画和颜色
void example_custom_style() {
    RainbowAnimation rainbowAnim;
    pulse::PulseBar bar(100, 50, "处理中",
                        pulse::ColorType::BRIGHT_YELLOW,
                        pulse::ColorType::BRIGHT_WHITE,
                        &rainbowAnim);

    // 设置渐变颜色回调
    bar.setColorBlendCallback([](int pos, int width, int percent) {
        //一半蓝色一半红色,我们可以通过设置不同的位置来触发不同的颜色
        if (pos < width / 2) return pulse::ColorType::BRIGHT_BLUE;
        else
            return pulse::ColorType::BRIGHT_RED;
    });

    // 设置动态括号回调
    bar.setBracketCallback([](int percent) {
        if (percent < 30) return std::make_pair(std::string("<<"), std::string(">>"));
        else if (percent < 70)
            return std::make_pair(std::string("{"), std::string("}"));
        else
            return std::make_pair(std::string("⟪"), std::string("⟫"));
    });

    for (int i = 0; i <= 100; ++i) {
        bar.update(i);
        std::this_thread::sleep_for(30ms);
    }
    bar.complete();
    pulse::PulseBar::newline();
}

// 示例3: 嵌套进度条
void example_nested() {
    auto process_item = [](int item_id, int total_items) {
        pulse::PulseBar bar(100, 40, "项目 " + std::to_string(item_id));
        for (int i = 0; i <= 100; i += rand() % 5 + 1) {
            bar.update(i);
            std::this_thread::sleep_for(20ms);
        }
        bar.complete();
    };

    const int num_items = 5;
    pulse::PulseBar main_bar(num_items, 30, "总体进度");
    for (int i = 0; i < num_items; i++) {
        process_item(i + 1, num_items);
        main_bar.update(i + 1);
        pulse::PulseBar::newline();
    }
    main_bar.complete();
}

//todo 修复多线程的只能显示一个的问题
// 示例4: 多线程
void example_multithreaded() {
    auto worker_task = [](int id, int total_work) {
        pulse::PulseBar bar(total_work, 40, "工作线程 " + std::to_string(id),
                            pulse::ColorType::BRIGHT_BLUE);
        for (int i = 0; i <= total_work; i++) {
            bar.update(i);
            std::this_thread::sleep_for(30ms);
        }
        bar.complete();
    };

    const int num_workers = 4;
    std::vector<std::thread> workers;
    for (int i = 0; i < num_workers; i++) {
        workers.emplace_back(worker_task, i + 1, 100);
    }
    for (auto &t: workers) {
        t.join();
    }
}

// 示例5: 动态标签更新
void example_set_label() {
    pulse::PulseBar bar(100, 50, "初始化");
    for (int i = 0; i <= 100; ++i) {
        if (i == 20) {
            bar.setLabel("加载配置");
        } else if (i == 60) {
            bar.setLabel("处理数据");
        }
        bar.update(i);
        std::this_thread::sleep_for(20ms);
    }
    bar.complete();
}

// 示例6: 毫秒时间格式
void example_milliseconds_time() {
    pulse::PulseBar bar(100, 50, "精确计时");

    // 用户可以选择设置时间格式，默认为秒
    bar.setTimeFormat("%S.%3N");// 显示秒和毫秒

    // 时间显示的颜色也可以修改
    bar.setTimeColor(pulse::ColorType::BRIGHT_YELLOW);

    for (int i = 0; i <= 100; ++i) {
        bar.update(i);
        std::this_thread::sleep_for(20ms);// 更快地更新以测试时间变化
    }
    bar.complete();
}

int main() {
    std::cout << "=== 示例1: 基本用法 ===\n";
    example_basic();

    std::cout << "\n=== 示例2: 自定义样式 ===\n";
    example_custom_style();

    std::cout << "\n=== 示例3: 嵌套进度条 ===\n";
    example_nested();

    std::cout << "\n=== 示例4: 多线程 ===\n";
    example_multithreaded();

    std::cout << "\n=== 示例5: 动态标签 ===\n";
    example_set_label();

    std::cout << "\n=== 示例6: 毫秒时间格式 ===\n";
    example_milliseconds_time();

    std::cout << "\n所有示例运行完成!\n";
    return 0;
}