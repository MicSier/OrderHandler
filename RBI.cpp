#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <queue>
#include <chrono>
#include <fstream>

#include <cstring>
#include "common.cpp"

std::mutex file_mutex;
const std::string filename = "OrderPool.txt";

struct FinancialInstrument {
    std::string id;
    std::string isin;
    FinancialInstrument(const std::string& id, const std::string& isin) : id(id), isin(isin) {}
};

enum class State
{
    New,
    Pending,
    Filled
};

std::string enumStatetostring(State s)
{
    switch (s)
    {
    case State::New     :    return "New";
    case State::Pending :    return "Pending";
    case State::Filled  :    return "Filled";
    }
    //unreachable
    exit(1);
}
struct Order {
    FinancialInstrument instrument;
    double price;
    double volume;
    State state;
    Order(const FinancialInstrument& instrument, const double& price, const double& volume, const State& state) : instrument(instrument), price(price), volume(volume), state(state) {}
};

std::queue<Order> orderPool;
std::mutex pool_mutex;
std::condition_variable condition_var;
bool streaming_done;

#define Data_Time std::chrono::system_clock::time_point

struct MarketData {
    FinancialInstrument instrument;
    double price;
    Data_Time timestamp;
    MarketData(const FinancialInstrument& instrument, const double& price, const Data_Time& timestamp) : instrument(instrument), price(price), timestamp(timestamp) {}
};

struct Trading_Strategy {
    std::vector<FinancialInstrument> instruments;
    std::vector<Order> orders;
    std::string name;
    Trading_Strategy(const std::string& name) : instruments({}), orders({}), name(name) {}
};

// abstract class 
class EventListener {
public:
    virtual void onEvent(const MarketData& trigger) = 0;
    virtual ~EventListener() {}
};

// class that is resposible for pushing Market Data to listeners
class EventSource {
public:
    void addListener(EventListener* listener) {
        listeners_.push_back(listener);
    }

    void fireEvent(const MarketData& trigger) {
        std::lock_guard<std::mutex> lock(trigger_queue_mutex_);
        trigger_queue_.push(trigger);
        cv_.notify_all();
    }

    MarketData getNextData() {
        std::unique_lock<std::mutex> lock(trigger_queue_mutex_);
        cv_.wait(lock, [this]() { return !trigger_queue_.empty(); });

        MarketData trigger = trigger_queue_.front();
        trigger_queue_.pop();
        return trigger;
    }

private:
    std::vector<EventListener*> listeners_;
    std::queue<MarketData> trigger_queue_;
    std::mutex trigger_queue_mutex_;
    std::condition_variable cv_;
};

// class checking strategy condition on maket data trigger and appending to streaming queue if condition is met
class MarketDataListener : public EventListener {
public:
    MarketDataListener(int id, EventSource* eventSource)
        : id_(id), eventSource_(eventSource), stop_(false) {}

    void onEvent(const MarketData& trigger) override {
        std::lock_guard<std::mutex> lock(listener_mutex_);
        std::cout << "Listener " << id_ << " received Market_Data:" << std::endl
            << "instrument: (" << trigger.instrument.id << ", " << trigger.instrument.isin << ") "
            << "price: " << trigger.price << std::endl;
        
   
        // TO DO: make strategy more general using strategy class
        double priceTreshold = 100.0;
        if (trigger.price >= priceTreshold)
        {
            Order newOrder (trigger.instrument, trigger.price, 100, State::New); 

            // Lock the queue and push the order
            {
                std::lock_guard<std::mutex> lock(pool_mutex);
                orderPool.push(newOrder);
            }
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

            MarketData trigger = eventSource_->getNextData();
            onEvent(trigger);
        }
    }

    int id_;
    EventSource* eventSource_;
    std::thread listener_thread_;
    std::mutex listener_mutex_;
    std::mutex stop_mutex_;    
    bool stop_;
};

std::ofstream file;

// Streamer function to write orders to the file asynchronously from the queue
void fileStreamer() {

    while (true) {
        std::unique_lock<std::mutex> lock(pool_mutex);
        
        condition_var.wait(lock, []{ return !orderPool.empty() || streaming_done; });

        while (!orderPool.empty()) {
            Order order = orderPool.front();
            orderPool.pop();
            std::lock_guard<std::mutex> guard(file_mutex);
            file << "instrument: (" << order.instrument.id << ", " << order.instrument.isin << ") " << std::endl
                << "price: " << order.price << std::endl
                << "Volume: " << order.volume << std::endl
                << "state:  " << enumStatetostring(order.state) << std::endl;
        }

        if (streaming_done && orderPool.empty()) {
            break;
        }
    }
    
}

// class for grabing dummy market data from Market Data server using sockets
class EventGenerator {
public:
    EventGenerator(EventSource* eventSource) : eventSource_(eventSource), stop_(false) {}

    void start() {

        init_sockets();

        sock = 0;
        struct sockaddr_in serv_addr;
        char buffer[BUFFER_SIZE] = {0};

        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            std::cerr << "Socket creation error" << std::endl;
            exit(1);
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(PORT);
        if (inetPtonCross(serv_addr) <= 0) {
            std::cerr << "Invalid address/ Address not supported" << std::endl;
            exit(1);
        }
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            std::cerr << "Connection failed" << std::endl;
            exit(1);
        }
        generator_thread_ = std::thread([this]() { run(); });

    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(generator_mutex_);
            stop_ = true;
        }
        generator_thread_.join();

        std::string message = encrypt_decrypt("stop");
        send(sock, message.c_str(), message.length(), 0);

        std::cout << "Encrypted message sent." << std::endl;
    }

private:
    void run() {
        int event_id = 1;
        while (true) {
            {
                std::lock_guard<std::mutex> lock(generator_mutex_);
                if (stop_) break;
            }

            std::string message =  encrypt_decrypt("market US0378331005");
            send(sock, message.c_str(), message.length(), 0);

            std::cout << "Encrypted message sent." << std::endl;

            char buffer[BUFFER_SIZE] = {0};
            int valread = readCross(sock, buffer, BUFFER_SIZE);
            std::string decrypted = encrypt_decrypt(std::string(buffer));  
            std::cout << "Received (decrypted): " << decrypted << std::endl;
            std::string isin_received = chop_by_delimiter(decrypted, ' ');
            FinancialInstrument fi("1", isin_received);
            double price_recived = std::stod(chop_by_delimiter(decrypted, ' '));
            MarketData md(fi, price_recived, Data_Time());
            eventSource_->fireEvent(md);

        }
    }

    EventSource* eventSource_;
    std::thread generator_thread_;
    std::mutex generator_mutex_;
    bool stop_;
    int sock;
};


int main() {
    // Open the file once and keep it open during the program's life
    file=std::ofstream(filename, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "Unable to open file for writing!" << std::endl;
        return 1;
    }

    EventSource eventSource;
    const int n_threads = 5;

    std::vector<std::thread> streamers;
    std::vector<std::unique_ptr<MarketDataListener>> listeners;

    for (int i = 0; i < n_threads; i++)
    {
        streamers.emplace_back(fileStreamer);
        listeners.emplace_back(std::make_unique<MarketDataListener>(i + 1, &eventSource));
    }
    
    for (const auto& listener : listeners)
        listener->start();

    EventGenerator eventGenerator(&eventSource);
    eventGenerator.start();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Stop listeners before stoping eventGenerator
    for (const auto& listener : listeners)
        listener->stop();

    eventGenerator.stop();
    // Indicate that writing is done and notify all streamers thread
    {
        std::lock_guard<std::mutex> lock(pool_mutex);
        streaming_done = true;
    }
    condition_var.notify_all();

    // Wait for streamers to finish
    for (auto& streamer: streamers)
        streamer.join();

    
    std::cout << "Event generation and listeners stopped." << std::endl;
    file.close();  

    return 0;
}
