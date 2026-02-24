#pragma once

#include <string>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>

class TokenManager {
public:
    explicit TokenManager(std::string path = defaultPath())
        : path_(std::move(path)) {}

    std::string load() const
    {
        std::ifstream f(path_);
        if (!f)
            throw std::runtime_error("Ingen token hittad. Kör: gridguard login <token>");
        std::string tok;
        std::getline(f, tok);
        if (tok.empty())
            throw std::runtime_error("Token-filen är tom. Kör: gridguard login <token>");
        return tok;
    }

    void save(const std::string& token) const
    {
        std::string dir = path_.substr(0, path_.rfind('/'));
        mkdir(dir.c_str(), 0700);

        std::ofstream f(path_, std::ios::trunc);
        if (!f)
            throw std::runtime_error("Kunde inte skriva till " + path_);
        f << token << '\n';
    }

    bool exists() const
    {
        return std::ifstream(path_).good();
    }

private:
    std::string path_;

    static std::string defaultPath()
    {
        const char* home = getenv("HOME");
        if (!home)
            throw std::runtime_error("$HOME är inte satt");
        return std::string(home) + "/.gridguard/token";
    }
};
