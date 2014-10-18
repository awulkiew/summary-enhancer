// Copyright 2014 Adam Wulkiewicz.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <fstream>
#include <iostream>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/algorithm/string/regex.hpp>

#include <boost/network.hpp>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"

std::string const tests_url = "http://www.boost.org/development/tests/";
std::string const branch = "develop";
std::string const view = "developer";
std::string const branch_url = tests_url + branch + "/";
std::string const view_url = branch_url + view + "/";

inline std::string get_document(std::string const& url, std::string & error_msg)
{
    std::string body;

    error_msg.clear();

    try
    {
        using namespace boost;
        using namespace boost::network;
        using namespace boost::network::http;

        http::client::request request_(url);
        request_ << network::header("Host", "www.boost.org");
        request_ << network::header("Connection", "keep-alive");
        http::client client_;
        http::client::response response_ = client_.get(request_);
        body = http::body(response_);

        if ( body.empty() )
        {
            error_msg = "Unknown error.";
        }
    }
    catch (std::exception & e)
    {
        error_msg = e.what();
    }

    return body;
}

template <typename NorA>
inline std::string name(NorA * n)
{
    if ( n )
        return std::string(n->name(), n->name_size());
    else
        return std::string();
}

template <typename NorA>
inline std::string value(NorA * n)
{
    if ( n )
        return std::string(n->value(), n->value_size());
    else
        return std::string();
}

template <typename NorA>
inline void set_value(rapidxml::xml_document<> & doc, NorA * n, const char* v)
{
    char * cstr = doc.allocate_string(v);

    n->value(cstr);
    if ( n->first_node("") )
        n->first_node("")->value(cstr);
}

struct fail_node
{
    fail_node(rapidxml::xml_node<> * td_,
              rapidxml::xml_node<> * a_,
              rapidxml::xml_attribute<> * href_,
              std::string const& log_url_)
        : td(td_), a(a_), href(href_), log_url(log_url_)
    {}
    rapidxml::xml_node<> * td;
    rapidxml::xml_node<> * a;
    rapidxml::xml_attribute<> * href;
    std::string log_url;
};

struct fail_nodes
    : public std::vector<fail_node>
{
    typedef std::vector<fail_node> base_t;

    fail_nodes(rapidxml::xml_document<> & doc)
    {
        gather_nodes(doc.first_node());
    }

private:
    inline void gather_nodes(rapidxml::xml_node<> * n)
    {
        if ( n == NULL )
            return;

        // "fail" <td>
        if ( "td" == name(n) )
        {
            rapidxml::xml_attribute<> * attr = n->first_attribute("class");

            if ( attr && "library-fail-unexpected-new" == value(attr) )
            {
                // "fail" <a>
                rapidxml::xml_node<> * anch = n->first_node("a");
                if ( anch )
                {
                    rapidxml::xml_attribute<> * href_attr = anch->first_attribute("href");
                    if ( href_attr )
                    {
                        // "fail link"
                        std::string href = value(href_attr);

                        // make global URL from local URL
                        if ( href.find("http://") == std::string::npos )
                        {
                            // remove prefixing '/'
                            while ( !href.empty() && href[0] == '/' )
                            {
                                href.erase(href.begin());
                            }

                            href = branch_url + href;
                        }

                        this->push_back(fail_node(n, anch, href_attr, href));
                    }
                }
            }
        }

        // depth first
        gather_nodes(n->first_node());
        gather_nodes(n->next_sibling());
    }
};

struct logs_pool
{
    typedef boost::network::http::basic_client<boost::network::http::tags::http_async_8bit_udp_resolve, 1, 1> client_type;
    typedef std::vector<fail_node>::iterator fail_iterator;
    
    struct element
    {
        element(fail_iterator it_, client_type::response const& response_)
            : fail_it(it_), response(response_), counter(0)
        {}

        fail_iterator fail_it;
        client_type::response response;
        int counter;
    };

    struct log_info
    {
        log_info(fail_iterator it_, std::string const& log_)
            : fail_it(it_), log(log_)
        {}

        fail_iterator fail_it;
        std::string log;
    };

    typedef std::vector<element>::iterator response_iterator;

    logs_pool(std::size_t max_requests_ = 10)
        : max_requests(max_requests_)
        , max_retries(3)
    {}

    fail_iterator add(fail_iterator first, fail_iterator last)
    {
        for ( ; first != last && responses.size() < max_requests ; ++first )
        {
            client_type::request request(first->log_url);
            request << boost::network::header("Host", "www.boost.org");
            request << boost::network::header("Connection", "keep-alive");
            
            responses.push_back(element(first, client.get(request)));
        }

        return first;
    }

    template <typename OutIt>
    void get(OutIt out)
    {
        for ( response_iterator it = responses.begin() ; it != responses.end() ; ++it )
        {
            if ( boost::network::http::ready(it->response) )
            {
                std::string body;

                try
                {
                    body = boost::network::http::body(it->response);
                    it->counter = -1;
                }
                catch (std::exception & e)
                {
                    // re-try
                    if ( it->counter < max_retries )
                    {
                        client_type::request request(it->fail_it->log_url);
                        request << boost::network::header("Host", "www.boost.org");
                        request << boost::network::header("Connection", "keep-alive");
                        it->response = client.get(request);
                        it->counter++;

                        std::cout << "Retrying!" << std::endl;
                    }
                    else
                    {
                        std::cerr << "ERROR! " << e.what() << std::endl;
                        it->counter = -1;
                    }
                }

                *out++ = log_info(it->fail_it, body);
            }
        }

        response_iterator it = std::remove_if(responses.begin(), responses.end(), is_not_active);
        responses.erase(it, responses.end());
    }

    static bool is_not_active(element const& el) { return el.counter < 0; }

    int max_retries;
    std::size_t max_requests;
    client_type client;
    std::vector<element> responses;
};

inline void modify_nodes(rapidxml::xml_document<> & doc,
                         fail_node & n,
                         std::string const& log)
{
    std::cout << "Processing: " << n.log_url.substr(n.log_url.find_last_of('/') + 1) << std::endl;
    
    // time limit exceeded
    if ( log.find("second time limit exceeded") != std::string::npos )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #88ff00;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "time");
    }
    // File too big or /bigobj
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("((File too big)|(/bigobj))"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #00ff88;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "file");
    }
    // internal compiler error
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("((internal compiler error)|(internal error))"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ff88ff;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "ierr");
    }
    // compilation failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Compile).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffbb00;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "comp");
    }
    // linking failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Link).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffdd00;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "link");
    }
    // run failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Run).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffff00;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "run");
    }
    // unknown fail
    else
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffff88;");
        n.td->append_attribute(style_attr);

        set_value(doc, n.a, "unkn");
    }
}

inline void process_fail(rapidxml::xml_document<> & doc, fail_node & n, std::string const& log)
{
    // remove spaces
    while ( n.td->first_node("") )
    {
        n.td->remove_node(n.td->first_node(""));
    }

    // set new, global href
    n.href->value( doc.allocate_string(n.log_url.c_str()) );

    modify_nodes(doc, n, log);
}

inline void print_log_urls(fail_nodes::iterator first, fail_nodes::iterator last)
{
    // Print
    for ( ; first != last ; ++first )
    {
        std::cout << "Downloading: " << first->log_url.substr(first->log_url.find_last_of('/') + 1) << std::endl;
    }
}

inline void process_document(std::string const& name, std::string & in, std::string & out)
{
    out.clear();
    if ( in.empty() )
        return;

    rapidxml::xml_document<> doc;
    doc.parse<0>(&in[0]); // non-98-standard but should work

    fail_nodes nodes(doc);
    logs_pool pool(50);

    fail_nodes::iterator it = nodes.begin();
    
    while ( it != nodes.end() || !pool.responses.empty() )
    {
        // new portion
        fail_nodes::iterator new_it = pool.add(it, nodes.end());

        print_log_urls(it, new_it);
        it = new_it;

        // wait a while
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));

        // get downloaded logs
        std::vector<logs_pool::log_info> logs;
        pool.get(std::back_inserter(logs));

        for ( std::vector<logs_pool::log_info>::iterator log_it = logs.begin() ;
              log_it != logs.end() ; ++log_it )
        {
            process_fail(doc, *(log_it->fail_it), log_it->log);
        }
    }

    std::cout << "Saving: " << name << std::endl;

    rapidxml::print(std::back_inserter(out), doc);
}

int main(int argc, char **argv)
{
    if ( argc <= 1 || argc == 2 && argv[1] == std::string("--help") )
    {
        std::cout << "Usage: program library...\n\n";
        std::cout << "Example: program geometry geometry-index geometry-extensions\n\n";
        std::cout << "Pass space separated list of libraries. In sublibs names use dash instead of slash, e.g. geometry-index";
        std::cout << std::endl;
        return 0;
    }

    // prepare the environment
    try
    {
        // download CSS file if needed
        boost::filesystem::path css_path = "master.css";
        if ( !boost::filesystem::exists(css_path) )
        {
            std::cout << "Downloading style." << std::endl;

            std::string error_msg;
            std::string body = get_document(branch_url + "master.css", error_msg);
            if ( !error_msg.empty() )
            {
                std::cerr << "ERROR downloading style! " << std::endl;
                std::cerr << error_msg << std::endl;
                std::cerr << "Try downloading it manually e.g. from http://www.boost.org/development/tests/develop/master.css and placing in the working directory." << std::endl;
                return 1;
            }
            std::ofstream of("master.css", std::ios::trunc);
            of << body;
        }
        else
        {
            std::cout << "Style found." << std::endl;
        }

        // create output directory if needed
        boost::filesystem::path result_path = "result";
        if ( !boost::filesystem::exists(result_path) )
        {
            std::cout << "Creating output directory." << std::endl;

            boost::filesystem::create_directory(result_path);
        }
        else
        {
            std::cout << "Output directory found." << std::endl;
        }
    }
    catch (std::exception & e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << "You probably do not have enough access privileges." << std::endl;
        return 1;
    }

    for ( int i = 1 ; i < argc ; ++i )
    {
        std::string lib = argv[i];
        std::string url = view_url + lib + "_.html";

        std::cout << "Downloading: " << lib << std::endl;
        
        // download the summary page
        std::string error_msg;
        std::string body = get_document(url, error_msg);

        if ( !error_msg.empty() )
        {
            std::cerr << "ERROR! " << error_msg << std::endl;
            continue;
        }

        // process the summary page
        std::string processed_body;

        process_document(lib, body, processed_body);

        // save processed summary page
        std::ofstream of(std::string("result/") + lib + ".html", std::ios::trunc);
        of << processed_body;
        of.close();
    }

    return 0;
}
