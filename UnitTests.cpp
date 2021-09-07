#include "BaseCode.cpp"
#include <regex>

int main(int argc, char *argv[]) 
{
    srand(time(NULL)); //seed randomness
    char buffer[101];
    GenRandomMsg100(buffer);
    for(int i = 0; i < 100; ++i)
    {
        assert(buffer[i]>31 && buffer[i]<128);
    }
    
    std::chrono::time_point<std::chrono::high_resolution_clock> start = std::chrono::high_resolution_clock::now();
    usleep(1000);
    std::chrono::time_point<std::chrono::high_resolution_clock> end = std::chrono::high_resolution_clock::now();
    double timeDelta = std::chrono::duration<double>(end-start).count()*1000;
    assert(timeDelta>2.0 && timeDelta <18.0);


    std::regex percentage (validPercentageRegex);
    assert(std::regex_match ("10.3",percentage));
    assert(std::regex_match ("100.000",percentage));
    assert(std::regex_match ("100.",percentage));
    assert(std::regex_match ("100",percentage));
    assert(std::regex_match ("0",percentage));
    assert(std::regex_match ("0.",percentage));
    assert(std::regex_match ("0.0",percentage));
    assert(std::regex_match ("1",percentage));
    assert(std::regex_match ("99",percentage));
    assert(std::regex_match ("43.5",percentage));
    assert(std::regex_match ("1.2",percentage));
    assert(std::regex_match ("1.",percentage));
    assert(!std::regex_match ("101",percentage));
    assert(!std::regex_match ("100.1",percentage));
    assert(!std::regex_match ("-4",percentage));
    assert(!std::regex_match ("-100",percentage));
    assert(!std::regex_match ("-34",percentage));
    assert(!std::regex_match ("-34.5",percentage));
    assert(!std::regex_match ("-34.",percentage));

    std::regex pos_decimal (validDecimalNumberRegex);
    assert(std::regex_match ("10.3",pos_decimal));
    assert(std::regex_match ("100.000",pos_decimal));
    assert(std::regex_match ("100.",pos_decimal));
    assert(std::regex_match ("100",pos_decimal));
    assert(std::regex_match ("0",pos_decimal));
    assert(std::regex_match ("0.",pos_decimal));
    assert(std::regex_match ("0.0",pos_decimal));
    assert(std::regex_match ("1",pos_decimal));
    assert(std::regex_match ("99",pos_decimal));
    assert(std::regex_match ("43.5",pos_decimal));
    assert(std::regex_match ("1.2",pos_decimal));
    assert(std::regex_match ("1.",pos_decimal));
    assert(std::regex_match ("101",pos_decimal));
    assert(std::regex_match ("100.1",pos_decimal));
    assert(!std::regex_match ("-4",pos_decimal));
    assert(!std::regex_match ("-100",pos_decimal));
    assert(!std::regex_match ("-34",pos_decimal));
    assert(!std::regex_match ("-34.5",pos_decimal));
    assert(!std::regex_match ("-34.",pos_decimal));

    return 0;
}