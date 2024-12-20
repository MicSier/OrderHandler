#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>
#include <random>

#include "..\common.cpp"

struct StockData {
    std::string name;
    std::string isin;
    float price;
    float mi;
    float sigma;
};

int main() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::vector<StockData> universe = {
        {"AAPL","US0378331005",132,1.02,1.16},
        {"GOOG","US02079K3059",1470,1.02,1.03},
        {"MSFT","US5949181045",220,1.03,1.24 },
        {"TSLA","US88160R1014",660,1.01,1.4},
        {"AMZN","US0231351067",3220,1.03,1.06} 
    };
    std::vector< double> prices;
    

    init_sockets();

    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "Socket creation error" << std::endl;
        return -1;
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return -1;
    }

    std::cout << "Waiting for connection..." << std::endl;
    
    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        std::cerr << "Accept failed" << std::endl;
        return -1;
    }
    
    bool run = true;
    while (run)
    {
        char buffer[BUFFER_SIZE] = {0};

        int valread = readCross(new_socket, buffer, BUFFER_SIZE);
        std::string message = encrypt_decrypt(std::string(buffer, valread));  
        std::cout << "Received (decrypted): " << message << std::endl;
        std::string word = chop_by_delimiter(message, ' ');
        if (word == "stop") run = false;
        else if (word == "market")
        {
            std::string isin = chop_by_delimiter(message, ' ');
            auto it = std::find_if(universe.begin(), universe.end(), [isin](const StockData& s) {return s.isin == isin; });
            if (it != universe.end()) {
                std::string reply = isin+" "+std::to_string(it->price);
                reply = encrypt_decrypt(reply);
                send(new_socket, reply.c_str(), reply.length(), 0);
    
                std::cout << "Encrypted message sent." << std::endl;

            }
            else
            {
                std::cout << "Requested ISIN not supported" << std::endl;
            }

        }

        double dt = 0.0001;

        std::normal_distribution<> d; 
        if (dt != 0) d = std::normal_distribution<>(0, sqrt(dt));
        for (auto& stock : universe)
        {
            if (dt != 0)
            {
                //Symulating geometric browninan motion to provide dummy market data to the application
                stock.price = exp((stock.mi-stock.sigma*stock.sigma/2.0)*dt+stock.sigma*d(gen))*stock.price;
            }
        }
        
    }

    closeCross(new_socket);
    cleanup_sockets();
    return 0;
}
