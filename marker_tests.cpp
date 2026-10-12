
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <random>
#include <iostream>
#include <algorithm>

class MarkerController {
public:
    explicit MarkerController(size_t array_size)
        : array_(array_size, 0)
        , array_size_(array_size) {}

    ~MarkerController() {
        for (size_t i = 0; i < num_markers_; ++i) {
            if (!markers_terminate_[i]) {
                terminate_marker(i);
            }
        }
    }

    void start_markers(size_t num_markers) {
        num_markers_ = num_markers;
        marker_mutexes_.resize(num_markers);
        marker_cvs_.resize(num_markers);
        markers_stopped_.resize(num_markers, false);
        markers_terminate_.resize(num_markers, false);
        markers_continue_.resize(num_markers, false);
        marked_counts_.resize(num_markers, 0);
        last_blocked_index_.resize(num_markers, -1);

        for (size_t i = 0; i < num_markers; ++i) {
            threads_.emplace_back(&MarkerController::marker_thread_func, this, i);
        }

        {
            std::lock_guard<std::mutex> lock(start_mutex_);
            start_signal_ = true;
        }
        start_cv_.notify_all();
    }

    void wait_all_markers_stopped() {
        std::unique_lock<std::mutex> lock(main_mutex_);
        main_cv_.wait(lock, [this] {
            return std::all_of(markers_stopped_.begin(), markers_stopped_.end(),
                [](bool stopped) { return stopped; });
            });
    }

    void print_array() const {
        std::lock_guard<std::mutex> lock(array_mutex_);
        for (size_t i = 0; i < array_.size(); ++i) {
            std::cout << array_[i] << " ";
        }
        std::cout << std::endl;
    }

    void terminate_marker(size_t marker_index) {
        if (marker_index >= num_markers_) return;

        {
            std::lock_guard<std::mutex> lock(marker_mutexes_[marker_index]);
            markers_terminate_[marker_index] = true;
            markers_continue_[marker_index] = false;
        }
        marker_cvs_[marker_index].notify_one();

        if (threads_[marker_index].joinable()) {
            threads_[marker_index].join();
        }

        markers_stopped_[marker_index] = true;
        main_cv_.notify_all();
    }

    void continue_remaining_markers() {
        for (size_t i = 0; i < num_markers_; ++i) {
            if (!markers_terminate_[i]) {
                {
                    std::lock_guard<std::mutex> lock(marker_mutexes_[i]);
                    markers_continue_[i] = true;
                    markers_stopped_[i] = false;
                }
                marker_cvs_[i].notify_one();
            }
        }
    }

    bool all_markers_finished() const {
        return std::all_of(markers_terminate_.begin(), markers_terminate_.end(),
            [](bool term) { return term; });
    }

    size_t get_num_markers() const { return num_markers_; }

    std::vector<int> get_array_copy() const {
        std::lock_guard<std::mutex> lock(array_mutex_);
        return array_;
    }

    int get_marked_count(size_t marker_index) const {
        std::lock_guard<std::mutex> lock(array_mutex_);
        if (marker_index < marked_counts_.size()) {
            return marked_counts_[marker_index];
        }
        return 0;
    }

    int get_last_blocked_index(size_t marker_index) const {
        if (marker_index < last_blocked_index_.size()) {
            return last_blocked_index_[marker_index];
        }
        return -1;
    }

    void wait_for_marker_stopped(size_t marker_index,
        std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
        if (marker_index >= markers_stopped_.size()) return;
        std::unique_lock<std::mutex> lock(main_mutex_);
        main_cv_.wait_for(lock, timeout, [this, marker_index] {
            return markers_stopped_[marker_index];
            });
    }

private:
    void marker_thread_func(size_t marker_id) {
        std::random_device rd;
        std::mt19937 gen(rd());

        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            start_cv_.wait(lock, [this] { return start_signal_; });
        }

        while (true) {
            size_t index = static_cast<size_t>(gen()) % array_size_;

            bool should_wait = false;
            bool should_terminate = false;

            {
                std::lock_guard<std::mutex> lock(array_mutex_);
                if (array_[index] == 0) {
                    marked_counts_[marker_id]++;
                    array_[index] = static_cast<int>(marker_id + 1);
                }
                else {
                    last_blocked_index_[marker_id] = static_cast<int>(index);
                    should_wait = true;
                }
            }

            if (should_wait) {
                {
                    std::lock_guard<std::mutex> lock(main_mutex_);
                    markers_stopped_[marker_id] = true;
                }
                main_cv_.notify_one();

                std::unique_lock<std::mutex> lock(marker_mutexes_[marker_id]);
                marker_cvs_[marker_id].wait(lock, [this, marker_id] {
                    return markers_continue_[marker_id] || markers_terminate_[marker_id];
                    });

                should_terminate = markers_terminate_[marker_id];
                markers_continue_[marker_id] = false;
            }

            if (should_terminate) {
                {
                    std::lock_guard<std::mutex> lock(array_mutex_);
                    for (size_t i = 0; i < array_.size(); ++i) {
                        if (array_[i] == static_cast<int>(marker_id + 1)) {
                            array_[i] = 0;
                        }
                    }
                }
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    std::vector<int> array_;
    const size_t array_size_;
    size_t num_markers_ = 0;

    std::vector<std::thread> threads_;
    std::vector<bool> markers_stopped_;
    std::vector<bool> markers_terminate_;
    std::vector<bool> markers_continue_;
    std::vector<int> marked_counts_;
    std::vector<int> last_blocked_index_;

    mutable std::mutex array_mutex_;
    mutable std::mutex start_mutex_;
    std::condition_variable start_cv_;
    bool start_signal_ = false;

    mutable std::mutex main_mutex_;
    std::condition_variable main_cv_;

    std::deque<std::mutex> marker_mutexes_;
    std::deque<std::condition_variable> marker_cvs_;
};

TEST(MarkerTest, ArrayInitialization) {
    MarkerController controller(10);
    auto array = controller.get_array_copy();
    EXPECT_EQ(array.size(), 10);
    for (int val : array) {
        EXPECT_EQ(val, 0);
    }
}

TEST(MarkerTest, SingleMarkerMarksElements) {
    MarkerController controller(20);
    controller.start_markers(1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    controller.wait_for_marker_stopped(0);

    auto array = controller.get_array_copy();
    int marked_count = 0;
    for (int val : array) {
        if (val == 1) marked_count++;
    }

    EXPECT_GT(marked_count, 0);
    EXPECT_EQ(controller.get_marked_count(0), marked_count);

    controller.terminate_marker(0);

    array = controller.get_array_copy();
    for (int val : array) {
        EXPECT_EQ(val, 0);
    }
}

TEST(MarkerTest, MultipleMarkersWorkConcurrently) {
    MarkerController controller(30);
    controller.start_markers(3);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    controller.wait_for_marker_stopped(0);
    controller.wait_for_marker_stopped(1);
    controller.wait_for_marker_stopped(2);

    auto array = controller.get_array_copy();
    int total_marked = 0;
    for (int val : array) {
        if (val > 0) total_marked++;
    }

    EXPECT_GT(total_marked, 0);

    for (size_t i = 0; i < 3; ++i) {
        controller.terminate_marker(i);
    }

    array = controller.get_array_copy();
    for (int val : array) {
        EXPECT_EQ(val, 0);
    }
}

TEST(MarkerTest, TerminateMarkerClearsItsElements) {
    MarkerController controller(15);
    controller.start_markers(2);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    controller.wait_for_marker_stopped(0);
    controller.wait_for_marker_stopped(1);

    auto array_before = controller.get_array_copy();
    int marker2_count = 0;
    for (int val : array_before) {
        if (val == 2) marker2_count++;
    }

    controller.terminate_marker(0);

    auto array_after = controller.get_array_copy();
    int marker1_count_after = 0, marker2_count_after = 0;
    for (int val : array_after) {
        if (val == 1) marker1_count_after++;
        if (val == 2) marker2_count_after++;
    }

    EXPECT_EQ(marker1_count_after, 0);
    EXPECT_EQ(marker2_count_after, marker2_count);

    controller.terminate_marker(1);
}

TEST(MarkerTest, ContinueSignalResumesMarkers) {
    MarkerController controller(25);
    controller.start_markers(2);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    controller.wait_for_marker_stopped(0);
    controller.wait_for_marker_stopped(1);

    auto array_before = controller.get_array_copy();
    int total_before = 0;
    for (int val : array_before) {
        if (val > 0) total_before++;
    }

    controller.continue_remaining_markers();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    controller.wait_for_marker_stopped(0);
    controller.wait_for_marker_stopped(1);

    auto array_after = controller.get_array_copy();
    int total_after = 0;
    for (int val : array_after) {
        if (val > 0) total_after++;
    }

    EXPECT_GE(total_after, total_before);

    controller.terminate_marker(0);
    controller.terminate_marker(1);
}

TEST(MarkerTest, RandomSeedProducesDifferentPatterns) {
    MarkerController controller1(50);
    MarkerController controller2(50);

    controller1.start_markers(1);
    controller2.start_markers(1);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    controller1.wait_for_marker_stopped(0);
    controller2.wait_for_marker_stopped(0);

    auto array1 = controller1.get_array_copy();
    auto array2 = controller2.get_array_copy();

    controller1.terminate_marker(0);
    controller2.terminate_marker(0);

    bool different = false;
    for (size_t i = 0; i < array1.size(); ++i) {
        if (array1[i] != array2[i]) {
            different = true;
            break;
        }
    }

    EXPECT_TRUE(different);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
