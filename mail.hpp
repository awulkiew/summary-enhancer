// Copyright 2014 Adam Wulkiewicz.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#ifndef MAIL_HPP
#define MAIL_HPP


#include <fstream>
//#include <iostream>
#include <vector>
#include <string>
#include <sstream>

#include <boost/asio.hpp>

namespace mail {

void send_request(std::string const& req, boost::asio::ip::tcp::socket & socket, std::string const& postfix = "\r\n")
{
    boost::asio::streambuf request;
    std::ostream request_stream(&request);
    request_stream << req << postfix;
    boost::asio::write(socket, request);
}

void send_data(std::string const& data, boost::asio::ip::tcp::socket & socket)
{
    send_request(data, socket, "\r\n.\r\n");
}

void expect_response(unsigned code, boost::asio::ip::tcp::socket & socket)
{
    boost::asio::streambuf response;
    boost::asio::read_until(socket, response, "\r\n");
    
    // ignore
    if ( code == 0 )
        return;

    std::istream response_stream(&response);

    unsigned response_code = 0;
    response_stream >> response_code;

    if ( response_code != code )
    {
        std::string message;
        std::getline(response_stream, message);
        throw std::runtime_error(message);
    }
}

void send(std::string const& host,
          std::string const& service,
          std::vector<std::string> const& recipients,
          std::string const& subject,
          std::string const& message)
{
    std::string from = "geometry-regression@boost.org";

    using boost::asio::ip::tcp;

    boost::asio::io_service io_service;

    tcp::resolver resolver(io_service);
    tcp::resolver::query query(host, service);
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    tcp::socket socket(io_service);
    boost::asio::connect(socket, endpoint_iterator);

    expect_response(220, socket);
    send_request("HELO", socket);
    expect_response(250, socket);
    send_request("MAIL FROM:<" + from + ">", socket);
    expect_response(250, socket);
    for ( std::vector<std::string>::const_iterator it = recipients.begin() ;
            it != recipients.end() ; ++it )
    {
        send_request("RCPT TO:<" + *it + ">", socket);
        expect_response(250, socket);
    }
    send_request("DATA", socket);
    expect_response(354, socket);

    std::stringstream ss;
    ss << "From: " + from + "\r\n";
    ss << "To: ";
    for ( std::vector<std::string>::const_iterator it = recipients.begin() ;
            it != recipients.end() ; )
    {
        ss << *it;
        ++it;
        if ( it != recipients.end() )
            ss << ", ";
        else
            ss << "\r\n";
    }
    //ss << "Date: Sat, 25 Oct 2014 04:00:00 +0200" << "\r\n";
    //ss << "MIME-Version: 1.0" << "\r\n";
    //ss << "Content-Type: text/plain; charset=UTF-8" << "\r\n";
    ss << "Subject: " << subject <<  "\r\n";
    ss << "\r\n";
    ss << message;

    //std::cout << ss.str();

    send_data(ss.str(), socket);

    expect_response(250, socket);
    send_request("QUIT", socket);
    expect_response(221, socket);
}

struct config
{
    config(std::string const& filename)
    {
        std::ifstream mail_file(filename.c_str());
        if ( mail_file.is_open() )
        {
            std::getline(mail_file, host);
            std::getline(mail_file, service);
            while ( mail_file.good() )
            {
                std::string recipient;
                std::getline(mail_file, recipient);
                if ( !recipient.empty() )
                    recipients.push_back(recipient);
            }
        }

        if ( host.empty() || service.empty() || recipients.empty() )
            throw std::runtime_error("couldn't load config");
    }

    std::string host;
    std::string service;
    std::vector<std::string> recipients;
};

} // namespace mail

#endif // MAIL_HPP
