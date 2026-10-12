
#include <iostream>
#include<string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <random>
#include <memory>
#include <algorithm>
#include <limits>

class MarkerController {
public:
    explicit MarkerController(size_t array_size)
        : array_(array_size, 0)
        , array_size_(array_size) {}

    void start_markers(size_t num_markers) {
        num_markers_ = num_markers;
        markers_stopped_.assign(num_markers, false);
        markers_terminate_.assign(num_markers, false);
        markers_continue_.assign(num_markers, false);

        marker_mutexes_.reserve(num_markers);
        marker_cvs_.reserve(num_markers);
        for (size_t i = 0; i < num_markers; ++i) {
            marker_mutexes_.push_back(std::make_unique<std::mutex>());
            marker_cvs_.push_back(std::make_unique<std::condition_variable>());
        }

        threads_.reserve(num_markers);
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
        for (int value : array_) {
            std::cout << value << " ";
        }
        std::cout << std::endl;
    }

    bool is_terminated(size_t marker_index) const {
        return marker_index < num_markers_ && markers_terminate_[marker_index];
    }

    void terminate_marker(size_t marker_index) {
        if (marker_index >= num_markers_) return;

        {
            std::lock_guard<std::mutex> lock(*marker_mutexes_[marker_index]);
            markers_terminate_[marker_index] = true;
            markers_continue_[marker_index] = false;
        }
        marker_cvs_[marker_index]->notify_one();

        if (threads_[marker_index].joinable()) {
            threads_[marker_index].join();
        }

        {
            std::lock_guard<std::mutex> lock(main_mutex_);
            markers_stopped_[marker_index] = true;
        }
    }

    void continue_remaining_markers() {
        for (size_t i = 0; i < num_markers_; ++i) {
            bool terminated;
            {
                std::lock_guard<std::mutex> lock(*marker_mutexes_[i]);
                terminated = markers_terminate_[i];
            }
            if (terminated) continue;

            {
                std::lock_guard<std::mutex> lock(main_mutex_);
                markers_stopped_[i] = false;
            }
            {
                std::lock_guard<std::mutex> lock(*marker_mutexes_[i]);
                markers_continue_[i] = true;
            }
            marker_cvs_[i]->notify_one();
        }
    }

    size_t get_num_markers() const { return num_markers_; }

private:
    void marker_thread_func(size_t marker_id) {
        const int my_number = static_cast<int>(marker_id + 1);

        std::mt19937 rng(static_cast<unsigned int>(marker_id + 1));

        {
            std::unique_lock<std::mutex> lock(start_mutex_);
            start_cv_.wait(lock, [this] { return start_signal_; });
        }

        int marked_count = 0;

        while (true) {
            size_t index = static_cast<size_t>(rng()) % array_size_;

            int value;
            {
                std::lock_guard<std::mutex> lock(array_mutex_);
                value = array_[index];
            }

            if (value == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));

                {
                    std::lock_guard<std::mutex> lock(array_mutex_);
                    array_[index] = my_number;
                }
                ++marked_count;

                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            else {
                {
                    std::lock_guard<std::mutex> lock(cout_mutex_);
                    std::cout << "Marker " << my_number
                        << ": marked elements = " << marked_count
                        << ", blocked index = " << index << std::endl;
                }

                {
                    std::lock_guard<std::mutex> lock(main_mutex_);
                    markers_stopped_[marker_id] = true;
                }
                main_cv_.notify_all();

                bool should_terminate = false;
                {
                    std::unique_lock<std::mutex> lock(*marker_mutexes_[marker_id]);
                    marker_cvs_[marker_id]->wait(lock, [this, marker_id] {
                        return markers_continue_[marker_id] ||
                            markers_terminate_[marker_id];
                        });
                    should_terminate = markers_terminate_[marker_id];
                    markers_continue_[marker_id] = false;
                }

                if (should_terminate) {
                    {
                        std::lock_guard<std::mutex> lock(array_mutex_);
                        for (size_t i = 0; i < array_.size(); ++i) {
                            if (array_[i] == my_number) {
                                array_[i] = 0;
                            }
                        }
                    }
                    return;
                }
            }
        }
    }

    std::vector<int> array_;
    const size_t array_size_;
    size_t num_markers_ = 0;

    std::vector<std::thread> threads_;

    std::vector<bool> markers_stopped_;
    std::vector<bool> markers_terminate_;
    std::vector<bool> markers_continue_;

    std::vector<std::unique_ptr<std::mutex>> marker_mutexes_;
    std::vector<std::unique_ptr<std::condition_variable>> marker_cvs_;

    mutable std::mutex array_mutex_;
    mutable std::mutex cout_mutex_;

    std::mutex start_mutex_;
    std::condition_variable start_cv_;
    bool start_signal_ = false;

    std::mutex main_mutex_;
    std::condition_variable main_cv_;
};

size_t read_positive_number(const std::string& prompt) {
    size_t value = 0;
    while (true) {
        std::cout << prompt;
        if (std::cin >> value && value > 0) {
            return value;
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input, try again." << std::endl;
    }
}

int main() {
    size_t array_size = read_positive_number("Enter array size: ");

    MarkerController controller(array_size);

    size_t num_markers = read_positive_number("Enter number of marker threads: ");

    controller.start_markers(num_markers);

    size_t terminated_count = 0;

    while (terminated_count < num_markers) {
        controller.wait_all_markers_stopped();

        std::cout << "Array contents: ";
        controller.print_array();

        size_t marker_to_terminate = 0;
        while (true) {
            marker_to_terminate = read_positive_number(
                "Enter marker index to terminate (1-" +
                std::to_string(num_markers) + "): ");

            if (marker_to_terminate <= num_markers &&
                !controller.is_terminated(marker_to_terminate - 1)) {
                break;
            }
            std::cout << "This marker does not exist or is already terminated. "
                "Try again." << std::endl;
        }

        controller.terminate_marker(marker_to_terminate - 1);
        ++terminated_count;

        std::cout << "Array contents after termination: ";
        controller.print_array();

        controller.continue_remaining_markers();
    }

    std::cout << "All markers finished. Final array: ";
    controller.print_array();

    return 0;
}
