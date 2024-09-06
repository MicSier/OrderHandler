#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <chrono>
#include <fstream>

std::mutex file_mutex;
const std::string filename = "OrderPool.txt";

struct Financial_Instrument {
    std::string id;
    std::string isin;
    Financial_Instrument(const std::string& id, const std::string& isin) : id(id), isin(isin) {}
};

enum class State
{
    New,
    Pending,
    Filled
};

struct Order {
    Financial_Instrument instrument;
    double price;
    double volume;
    State state;
    Order(const Financial_Instrument& instrument, const double& price, const double& volume, const State& state) : instrument(instrument), price(price), volume(volume), state(state) {}
};

#define Data_Time std::chrono::system_clock::time_point

struct Market_Data {
    Financial_Instrument instrument;
    double price;
    Data_Time timestamp;
    Market_Data(const Financial_Instrument& instrument, const double& price, const Data_Time& timestamp) : instrument(instrument), price(price), timestamp(timestamp) {}
};

struct Trading_Strategy {
    std::vector<Financial_Instrument> instruments;
    std::vector<Order> orders;
    std::string name;
    Trading_Strategy(const std::string& name) : instruments({}), orders({}), name(name) {}
};

// data type for receving market data 
struct Event {
    int id;
    std::string message;
    Market_Data md;
};

// abstract class no longer needed
class EventListener {
public:
    virtual void onEvent(const Event& event) = 0;
    virtual ~EventListener() {}
};

// class that is resposible for pushing Market Data to listeners
class EventSource {
public:
    void addListener(EventListener* listener) {
        listeners_.push_back(listener);
    }

    void fireEvent(const Event& event) {
        std::lock_guard<std::mutex> lock(event_queue_mutex_);
        event_queue_.push(event);
        cv_.notify_all();
    }

    Event getNextEvent() {
        std::unique_lock<std::mutex> lock(event_queue_mutex_);
        cv_.wait(lock, [this]() { return !event_queue_.empty(); });

        Event event = event_queue_.front();
        event_queue_.pop();
        return event;
    }

private:
    std::vector<EventListener*> listeners_;
    std::queue<Event> event_queue_;
    std::mutex event_queue_mutex_;
    std::condition_variable cv_;
};

// class
class MarketDataListener : public EventListener {
public:
    MarketDataListener(int id, EventSource* eventSource)
        : id_(id), eventSource_(eventSource), stop_(false) {}

    void onEvent(const Event& event) override {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        std::cout << "Listener " << id_ << " received event: " << event.id
            << " with message: " << event.message << std::endl
            << "Market_Data:" << std::endl
            << "instrument: (" << event.md.instrument.id << ", " << event.md.instrument.isin << ") "
            << "price: " << event.md.price << std::endl;
        std::lock_guard<std::mutex> guard(file_mutex);
    
        // Open the file in append mode
        // TO DO: get rid of opening file inside onEvent
        // TO DO: Execute strategy check here and only stream when true
        std::ofstream file;
        file.open(filename, std::ios::app);

        if (file.is_open()) {
        file << "Listener " << id_ << " received event: " << event.id
            << " with message: " << event.message << std::endl
            << "Market_Data:" << std::endl
            << "instrument: (" << event.md.instrument.id << ", " << event.md.instrument.isin << ") "
            << "price: " << event.md.price << std::endl;
        file.close();
    } else {
        std::cerr << "Unable to open file." << std::endl;
    }
        double priceTreshold = 100.0;
        if (event.md.price >= priceTreshold)
        {
            Order newOrder (event.md.instrument, event.md.price, 100, State::New);
        }
    }

    void start() {
        listener_thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(stop_mutex_);
            stop_ = true;
        }
        listener_thread_.join();
    }

private:
    void run() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(stop_mutex_);
                if (stop_) break;
            }

            Event event = eventSource_->getNextEvent();
            onEvent(event);
        }
    }

    int id_;
    EventSource* eventSource_;
    std::thread listener_thread_;
    std::mutex listener_mutex_;
    std::mutex stop_mutex_;    
    bool stop_;
};

// class for generating dummy market data before I implement Market Data server
class EventGenerator {
public:
    EventGenerator(EventSource* eventSource) : eventSource_(eventSource), stop_(false) {}

    void start() {
        generator_thread_ = std::thread([this]() { run(); });
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(generator_mutex_);
            stop_ = true;
        }
        generator_thread_.join();
    }

private:
    void run() {
        int event_id = 1;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(generator_mutex_);
                if (stop_) break;
            }

            Financial_Instrument fi("1", "ISIN01");
            Market_Data md(fi, 100.0, Data_Time());
            Event event{event_id++, "Sample event message", md};
            std::cout << "Generated event: " << event.id << std::endl;
            eventSource_->fireEvent(event);

            // Sleep for a while to simulate time between events
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    EventSource* eventSource_;
    std::thread generator_thread_;
    std::mutex generator_mutex_;
    bool stop_;
};


int main() {
    EventSource eventSource;

    const int n_threads = 5;
    std::vector<std::unique_ptr<MarketDataListener>> listeners;

    for (int i = 0; i < n_threads; i++)
        listeners.emplace_back(std::make_unique<MarketDataListener>(i + 1, &eventSource));
    
    for (const auto& listener : listeners)
        listener->start();

    EventGenerator eventGenerator(&eventSource);
    eventGenerator.start();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stop listeners before stoping eventGenerator
    for (const auto& listener : listeners)
        listener->stop();

    eventGenerator.stop();
    std::cout << "Event generation and listeners stopped." << std::endl;

    return 0;
}
