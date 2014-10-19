// Copyright 2014 Adam Wulkiewicz.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <fstream>
#include <iostream>
#include <vector>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <boost/network.hpp>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"


struct options
{
    options()
        : verbose(false)
        , connections(5)
        , retries(3)
        , tests_url("http://www.boost.org/development/tests/")
        , branch("develop")
        , view("developer")
    {
        refresh();
    }

    void refresh()
    {
        branch_url = tests_url + branch + "/";
        view_url = branch_url + view + "/";
    }

    bool verbose;
    unsigned short connections;
    unsigned short retries;

    std::string tests_url;
    std::string branch;
    std::string view;

    std::string branch_url;
    std::string view_url;

    std::vector<std::string> libraries;
};

bool process_options(int argc, char **argv, options & op)
{
    try
    {
        std::stringstream msg;
        msg << "Usage: summary-enhancer [OPTIONS] library...\n\n";
        msg << "Pass space separated list of libraries. In sublibs names use hyphen (-) instead of slash (/), e.g. geometry-index\n\n";
        msg << "Example: summary-enhancer geometry geometry-index geometry-extensions\n\n";
        msg << "Options";

        namespace po = boost::program_options;
        po::options_description desc(msg.str());
        desc.add_options()
            ("help", "produce help message")
            ("connections", po::value<int>()->default_value(op.connections), "max number of connections [1..100]")
            ("retries", po::value<int>()->default_value(op.retries), "max number of retries [1..10]")
            ("branch", po::value<std::string>()->default_value(op.branch), "branch name {develop, master}")
            ("verbose", "show details")
            ;

        po::parsed_options parsed = 
            po::command_line_parser(argc, argv).options(desc).allow_unregistered().run();
        op.libraries = collect_unrecognized(parsed.options, po::include_positional);

        po::variables_map vm;
        po::store(parsed, vm);
        po::notify(vm);    

        if ( argc <= 1 || vm.count("help") || op.libraries.empty() )
        {
            std::cout << desc << "\n";
            return false;
        }

        bool result = true;

        if ( vm.count("verbose") )
            op.verbose = true;

        int c = vm["connections"].as<int>();
        if ( c < 1 || 100 < c )
        {
            std::cerr << "Invalid connections value" << std::endl;
            result = false;
        }
        op.connections = static_cast<unsigned short>(c);

        int r = vm["retries"].as<int>();
        if ( r < 1 || 10 < r )
        {
            std::cerr << "Invalid retries value" << std::endl;
            result = false;
        }
        op.retries = static_cast<unsigned short>(r);

        std::string b = vm["branch"].as<std::string>();
        if ( b != "develop" && b != "master" )
        {
            std::cerr << "Invalid branch" << std::endl;
            result = false;
        }
        op.branch = b;

        op.refresh();

        return result;
    }
    catch (std::exception & e)
    {
        std::cerr << "Error processing options: " << e.what() << std::endl;
    }

    return false;
}

std::string get_document(std::string const& url, std::string & error_msg)
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
std::string name(NorA * n)
{
    if ( n )
        return std::string(n->name(), n->name_size());
    else
        return std::string();
}

template <typename NorA>
std::string value(NorA * n)
{
    if ( n )
        return std::string(n->value(), n->value_size());
    else
        return std::string();
}

template <typename NorA>
void set_value(rapidxml::xml_document<> & doc, NorA * n, const char* v)
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

struct anchor_node
{
    anchor_node(rapidxml::xml_node<> * a_,
                rapidxml::xml_attribute<> * href_,
                std::string const& url_)
        : a(a_), href(href_), url(url_)
    {}
    rapidxml::xml_node<> * a;
    rapidxml::xml_attribute<> * href;
    std::string url;
};

bool not_slash(char c) { return c != '/' && c != '\\'; }

std::string to_global(std::string const& url, std::string const& global_prefix)
{
    // not global url
    if ( !boost::starts_with(url, "http://") &&
         !boost::starts_with(url, "https://") )
    {
        std::string::const_iterator it = std::find_if(url.begin(), url.end(), not_slash);
        return global_prefix + std::string(it, url.end());
    }

    return url;
}

struct nodes_containers
{
    typedef std::vector<fail_node> fails_container;
    typedef fails_container::iterator fails_iterator;

    typedef std::vector<anchor_node> anchors_container;
    typedef anchors_container::iterator anchors_iterator;

    nodes_containers(rapidxml::xml_document<> & doc, options const& op)
    {
        gather_nodes(doc.first_node(), op);
    }

    fails_container fails;
    anchors_container non_log_anchors;

private:
    void gather_nodes(rapidxml::xml_node<> * n, options const& op)
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
                        std::string global_href = to_global(value(href_attr), op.branch_url);
                        fails.push_back(fail_node(n, anch, href_attr, global_href));
                    }
                }
            }
        }

        // non-fail/log <a>
        if ( "a" == name(n) )
        {
            rapidxml::xml_attribute<> * class_attr = n->first_attribute("class");
            rapidxml::xml_attribute<> * href_attr = n->first_attribute("href");
            if ( ( class_attr == NULL || value(class_attr) != "log-link") && href_attr )
            {
                std::string global_href = to_global(value(href_attr), op.view_url);
                non_log_anchors.push_back(anchor_node(n, href_attr, global_href));
            }
        }

        // depth first
        gather_nodes(n->first_node(), op);
        gather_nodes(n->next_sibling(), op);
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

    logs_pool(options const& op)
        : max_requests(op.connections)
        , max_retries(op.retries)
        , verbose(op.verbose)
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

                        if ( verbose )
                            std::cout << "Retrying!" << std::endl;

                        continue;
                    }
                    else
                    {
                        std::cerr << "ERROR! " << e.what() << std::endl;
                    }
                }

                *out++ = log_info(it->fail_it, body);
                it->counter = -1;
            }
        }

        response_iterator it = std::remove_if(responses.begin(), responses.end(), is_not_active);
        responses.erase(it, responses.end());
    }

    static bool is_not_active(element const& el) { return el.counter < 0; }

    int max_retries;
    std::size_t max_requests;
    bool verbose;

    client_type client;
    std::vector<element> responses;
};

std::string filename_from_url(std::string const& url)
{
    return url.substr(url.find_last_of('/') + 1);
}

void modify_nodes(rapidxml::xml_document<> & doc,
                  fail_node & n,
                  std::string const& log,
                  options const& op)
{
    if ( op.verbose )
        std::cout << "Processing: " << filename_from_url(n.log_url) << std::endl;
    
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

void process_fail(rapidxml::xml_document<> & doc, fail_node & n, std::string const& log, options const& op)
{
    // remove spaces
    while ( n.td->first_node("") )
    {
        n.td->remove_node(n.td->first_node(""));
    }

    // set new, global href
    n.href->value( doc.allocate_string(n.log_url.c_str()) );

    modify_nodes(doc, n, log, op);
}

void process_anchor(rapidxml::xml_document<> & doc, anchor_node & n)
{
    // set new, global href
    n.href->value( doc.allocate_string(n.url.c_str()) );
}

void process_document(std::string const& name, std::string & in, std::string & out, options const& op)
{
    out.clear();
    if ( in.empty() )
        return;

    rapidxml::xml_document<> doc;
    doc.parse<0>(&in[0]); // non-98-standard but should work

    nodes_containers nodes(doc, op);
    
    // process fails

    logs_pool pool(op);
    
    nodes_containers::fails_iterator it = nodes.fails.begin();    

    while ( it != nodes.fails.end() || !pool.responses.empty() )
    {
        // new portion of logs
        nodes_containers::fails_iterator new_it = pool.add(it, nodes.fails.end());

        // print log names
        if ( op.verbose )
        {
            for ( ; it != new_it ; ++it )
                std::cout << "Downloading: " << filename_from_url(it->log_url) << std::endl;
        }

        // move "it" iterator to a new position
        it = new_it;

        // wait a while
        boost::this_thread::sleep(boost::posix_time::milliseconds(100));

        // get downloaded logs
        std::vector<logs_pool::log_info> logs;
        pool.get(std::back_inserter(logs));

        for ( std::vector<logs_pool::log_info>::iterator log_it = logs.begin() ;
              log_it != logs.end() ; ++log_it )
        {
            process_fail(doc, *(log_it->fail_it), log_it->log, op);
        }
    }

    // process anchors
    for ( nodes_containers::anchors_iterator a_it = nodes.non_log_anchors.begin() ;
          a_it != nodes.non_log_anchors.end() ; ++a_it )
    {
        process_anchor(doc, *a_it);
    }

    std::cout << "Saving: " << name << std::endl;

    rapidxml::print(std::back_inserter(out), doc);
}

int main(int argc, char **argv)
{
    // process program options
    options op;
    if ( !process_options(argc, argv, op) )
        return 1;

    // prepare the environment
    try
    {
        // download CSS file if needed
        boost::filesystem::path css_path = "master.css";
        if ( !boost::filesystem::exists(css_path) )
        {
            std::cout << "Downloading style." << std::endl;

            std::string error_msg;
            std::string body = get_document("http://www.boost.org/development/tests/develop/master.css", error_msg);
            if ( !error_msg.empty() )
            {
                std::cerr << "ERROR downloading style! " << std::endl;
                std::cerr << error_msg << std::endl;
                std::cerr << "You may try to download it manually from http://www.boost.org/development/tests/develop/master.css and place it in the working directory." << std::endl;
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

    // process all libraries
    for ( std::vector<std::string>::iterator it = op.libraries.begin() ;
          it != op.libraries.end() ; ++it )
    {
        std::string const& lib = *it;
        std::string url = op.view_url + lib + "_.html";

        std::cout << "Downloading: " << lib << std::endl;
        
        // download the summary page
        std::string error_msg;
        std::string body = get_document(url, error_msg);

        if ( !error_msg.empty() )
        {
            std::cerr << "ERROR! " << error_msg << std::endl;
            continue;
        }

        std::cout << "Processing." << std::endl;

        // process the summary page
        std::string processed_body;

        process_document(lib, body, processed_body, op);

        // save processed summary page
        std::ofstream of(std::string("result/") + op.branch + '-' + lib + ".html", std::ios::trunc);
        of << processed_body;
        of.close();
    }

    return 0;
}
