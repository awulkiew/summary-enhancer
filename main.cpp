// Copyright 2014 Adam Wulkiewicz.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <fstream>
#include <iostream>

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

inline void modify_nodes(rapidxml::xml_document<> & doc, rapidxml::xml_node<> * td, rapidxml::xml_node<> * a, std::string const& log_url)
{
    BOOST_ASSERT(td && a && !log_url.empty());

    std::cout << "Processing: " << log_url.substr(log_url.find_last_of('/') + 1) << std::endl;
    
    std::string error_msg;
    std::string log = get_document(log_url, error_msg);

    if ( !error_msg.empty() )
    {
        std::cerr << "ERROR! " << error_msg << std::endl;
        return;
    }

    // time limit exceeded
    if ( log.find("second time limit exceeded") != std::string::npos )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #88ff00;");
        td->append_attribute(style_attr);

        set_value(doc, a, "time");
    }
    // File too big
    else if ( log.find("File too big") != std::string::npos )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #00ff88;");
        td->append_attribute(style_attr);

        set_value(doc, a, "file");
    }
    // internal compiler error
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("((internal compiler error)|(internal error))"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ff88ff;");
        td->append_attribute(style_attr);

        set_value(doc, a, "ierr");
    }
    // compilation failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Compile).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffbb00;");
        td->append_attribute(style_attr);

        set_value(doc, a, "comp");
    }
    // linking failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Link).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", "background-color: #ffdd00;");
        td->append_attribute(style_attr);

        set_value(doc, a, "link");
    }
    // run failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Run).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        set_value(doc, a, "run");
    }
    // unknown fail
    else
    {
        set_value(doc, a, "unkn");
    }
}

inline void process_node(rapidxml::xml_document<> & doc, rapidxml::xml_node<> * n)
{
    if ( n == NULL )
        return;

    // "fail" <td>
    if ( "td" == name(n) )
    {
        rapidxml::xml_attribute<> * attr = n->first_attribute("class");

        if ( attr && "library-fail-unexpected-new" == value(attr) )
        {
            // remove spaces
            while ( n->first_node("") )
            {
                n->remove_node(n->first_node(""));
            }

            // "fail" <a>
            rapidxml::xml_node<> * anch = n->first_node("a");
            if ( anch )
            {
                rapidxml::xml_attribute<> * href_attr = anch->first_attribute("href");
                if ( href_attr )
                {
                    // "fail link"
                    std::string href = value(href_attr);

                    if ( href.find("BP") != std::string::npos )
                    {
                        int a = 10;
                    }

                    // make global URL from local URL
                    if ( href.find("http://") == std::string::npos )
                    {
                        // remove prefixing '/'
                        while ( !href.empty() && href[0] == '/' )
                        {
                            href.erase(href.begin());
                        }

                        href = branch_url + href;

                        char * href_cstr = doc.allocate_string(href.c_str());
                        href_attr->value(href_cstr);
                    }

                    modify_nodes(doc, n, anch, href);
                }
            }
        }
    }

    // depth first
    process_node(doc, n->first_node());
    process_node(doc, n->next_sibling());
}

inline void process_document(std::string & in, std::string & out)
{
    out.clear();
    if ( in.empty() )
        return;

    rapidxml::xml_document<> doc;
    doc.parse<0>(&in[0]); // non-98-standard but should work

    process_node(doc, doc.first_node());

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

        std::cout << "Processing: " << lib << std::endl;
        
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
        process_document(body, processed_body);

        // save processed summary page
        std::ofstream of(std::string("result/") + lib + ".html", std::ios::trunc);
        of << processed_body;
        of.close();
    }

    return 0;
}
