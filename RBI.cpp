#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <queue>
#include <chrono>
#include <fstream>

#include <cstring>
#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
    #include <Ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
#endif

const int PORT = 8080;
const int BUFFER_SIZE = 1024;

std::string encrypt_decrypt(std::string data) {
    const char XOR_KEY = 0xAA; // Simple XOR key

    for (size_t i = 0; i < data.size(); ++i) {
        data[i] ^= XOR_KEY;
    }
    return data;
}

std::string chop_by_delimiter(std::string &input, char delimiter) {
    // Find the position of the delimiter in the input string
    size_t delimiterPos = input.find(delimiter);
    
    // If the delimiter is not found, return the entire input string and clear the input string
    if (delimiterPos == std::string::npos) {
        std::string result = input;
        input.clear();
        return result;
    }
    
    // Extract the substring before the delimiter
    std::string result = input.substr(0, delimiterPos);
    
    // Erase the part of the input string before the delimiter
    input.erase(0, delimiterPos + 1);
    
    // Remove white space characters from the result
    result.erase(std::remove_if(result.begin(), result.end(), [](unsigned char c) { return std::isspace(c); }), result.end());

    // Return the extracted substring
    return result;
}
void init_sockets() {
#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

void cleanup_sockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

int inetPtonCross(sockaddr_in& service) {
#ifdef _WIN32 
    return InetPton(AF_INET, __TEXT("127.0.0.1"), &service.sin_addr);
#elif         
    return inet_pton(AF_INET, "127.0.0.1", &service.sin_addr);
#endif        
}

int readCross(int socket, char* buffer, const int BUFFER_SIZE) {
#ifdef _WIN32 
    return recv(socket, buffer, BUFFER_SIZE,0);
#elif         
    return read(socket, buffer, BUFFER_SIZE);
#endif        
}

void closeCross(int socket) {
#ifdef _WIN32
    closesocket(socket);  
#elif
    close(socket);      
#endif
}

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
    Financial_Instrument instrument;
    double price;
    double volume;
    State state;
    Order(const Financial_Instrument& instrument, const double& price, const double& volume, const State& state) : instrument(instrument), price(price), volume(volume), state(state) {}
};

std::queue<Order> orderPool;
std::mutex pool_mutex;
std::condition_variable condition_var;
bool streaming_done;

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
        
   
        // TO DO: make strategy more general using strategy class
        double priceTreshold = 100.0;
        if (event.md.price >= priceTreshold)
        {
            Order newOrder (event.md.instrument, event.md.price, 100, State::New); 

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

std::ofstream file;

// Stremer function to write orders to the file asynchronously
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
            Financial_Instrument fi("1", isin_received);
            double price_recived = std::stod(chop_by_delimiter(decrypted, ' '));
            Market_Data md(fi, price_recived, Data_Time());
            Event event{event_id++, "Sample event message", md};
            std::cout << "Generated event: " << event.id << std::endl;
            eventSource_->fireEvent(event);

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
