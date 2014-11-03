// Copyright 2014 Adam Wulkiewicz.

// Use, modification and distribution is subject to the Boost Software License,
// Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)


#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <set>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <boost/archive/xml_iarchive.hpp>
#include <boost/archive/xml_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>

#include <boost/network.hpp>

#include "rapidxml/rapidxml.hpp"
#include "rapidxml/rapidxml_print.hpp"

#include "mail.hpp"

struct options
{
    options()
        : verbose(false)
        , track_changes(false)
        , send_report(false)
        , save_report(false)
        , log_format(xml)
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
    
    bool track_changes;
    bool send_report;
    bool save_report;
    enum { binary, xml } log_format;

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
        ("track-changes", "compare failures with the previous run")
        ("log-format", po::value<std::string>()->default_value("xml"), "the format of failures log {xml, binary}")
        ("send-report", "send an email containing the report about the failures")
        ("save-report", "save report to file")
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

    if ( vm.count("track-changes") )
        op.track_changes = true;

    if ( vm.count("send-report") )
        op.send_report = true;

    if ( vm.count("save-report") )
        op.save_report = true;
    
    std::string lf = vm["log-format"].as<std::string>();
    if ( lf != "xml" && lf != "binary" )
    {
        std::cerr << "Invalid log format" << std::endl;
        result = false;
    }
    op.log_format = lf == "binary" ? options::binary : options::xml;

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

std::string get_document(std::string const& url)
{
    using namespace boost;
    using namespace boost::network;
    using namespace boost::network::http;

    http::client::request request_(url);
    request_ << network::header("Host", "www.boost.org");
    request_ << network::header("Connection", "keep-alive");
    http::client client_;
    http::client::response response_ = client_.get(request_);
    return http::body(response_);
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

template <typename T, typename NorA>
T value_as(NorA * n, T def)
{
    T res = def;
    try {
        res = boost::lexical_cast<T>(value(n));
    } catch (...) {}
    return res;
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
              std::string const& log_url_,
              std::size_t toolset_index_,
              std::string const& test_name_)
        : td(td_), a(a_), href(href_), log_url(log_url_)
        , toolset_index(toolset_index_)
        , test_name(test_name_)
    {}
    rapidxml::xml_node<> * td;
    rapidxml::xml_node<> * a;
    rapidxml::xml_attribute<> * href;
    std::string log_url;
    std::size_t toolset_index;
    std::string test_name;
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

    typedef std::vector<std::string> strings_container;
    typedef strings_container::iterator strings_iterator;

    nodes_containers(rapidxml::xml_document<> & doc, options const& op)
    {
        gathering_state state;
        gather_nodes(doc.first_node(), op, state);
    }

    fails_container fails;
    anchors_container non_log_anchors;

    strings_container runners;
    strings_container toolsets;

private:
    struct gathering_state
    {
        gathering_state()
            : toolset_index(0)
            , table_footer_counter(0)
        {}

        std::size_t toolset_index;
        std::string test_name;
        int table_footer_counter;
    };

    void gather_nodes(rapidxml::xml_node<> * n, options const& op,
                      gathering_state & state)
    {
        if ( n == NULL )
            return;

        std::string tag = name(n);

        // "fail" <td>
        if ( "td" == tag )
        {
            std::string class_name = value(n->first_attribute("class"));

            if ( "runner" == class_name )
            {
                // ignore footer
                if ( state.table_footer_counter == 0 )
                {
                    // runner <a>
                    rapidxml::xml_node<> * a = n->first_node("a");
                    if ( a )
                    {
                        // colspan attribute
                        int colspan = value_as<int>(n->first_attribute("colspan"), 1);
                        if ( colspan < 1 )
                            colspan = 1;

                        std::string runner = value(a);
                        boost::trim(runner);
                        runners.insert(runners.end(), colspan, runner);
                    }
                }
            }
            else if ( "toolset-name" == class_name
                   || "required-toolset-name" == class_name )
            {
                // ignore footer
                if ( state.table_footer_counter == 0 )
                {
                    // toolset <span>
                    std::string name = value(n->first_node("span"));
                    boost::trim(name);
                    toolsets.push_back(name);
                }
            }
            else if ( "test-name" == class_name )
            {
                if ( runners.size() != toolsets.size() )
                    throw std::runtime_error("unexpected runners/toolsets number");

                std::string test_name = value(n->first_node("a"));
                boost::trim(test_name);
                state.test_name = test_name;
                state.toolset_index = 0;
            }
            else if ( "library-fail-unexpected-new" == class_name )
            {
                if ( state.toolset_index >= toolsets.size() )
                    throw std::runtime_error("unexpected toolsets/tests number");

                // "fail" <a>
                rapidxml::xml_node<> * anch = n->first_node("a");
                if ( anch )
                {
                    rapidxml::xml_attribute<> * href_attr = anch->first_attribute("href");
                    if ( href_attr )
                    {
                        // "fail link"
                        std::string global_href = to_global(value(href_attr), op.branch_url);
                        fails.push_back(fail_node(n, anch, href_attr, global_href, state.toolset_index, state.test_name));
                    }
                }

                ++state.toolset_index;
            }
            else if ( boost::starts_with(class_name, "library-") )
            {
                if ( state.toolset_index >= toolsets.size() )
                    throw std::runtime_error("unexpected toolsets/tests number");

                ++state.toolset_index;
            }
        }
        // non-fail/log <a>
        else if ( "a" == tag )
        {
            rapidxml::xml_attribute<> * class_attr = n->first_attribute("class");
            rapidxml::xml_attribute<> * href_attr = n->first_attribute("href");
            if ( ( class_attr == NULL || value(class_attr) != "log-link") && href_attr )
            {
                std::string global_href = to_global(value(href_attr), op.view_url);
                non_log_anchors.push_back(anchor_node(n, href_attr, global_href));
            }
        }
        
        if ( "tfoot" == tag )
        {
            // depth first
            ++state.table_footer_counter;
            gather_nodes(n->first_node(), op, state);
            --state.table_footer_counter;
        }
        else
        {
            // depth first
            gather_nodes(n->first_node(), op, state);
        }
        
        gather_nodes(n->next_sibling(), op, state);
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
                        std::cerr << "Error: " << e.what() << std::endl;
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

std::string find_reason(std::string const& log)
{
    // time limit exceeded
    if ( log.find("second time limit exceeded") != std::string::npos )
    {
        return "time";
    }
    // File too big or /bigobj
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("((File too big)|(/bigobj))"),
                    boost::match_not_dot_newline
                )) )
    {
        return "file";
    }
    // internal compiler error
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("((internal compiler error)|(internal error))"),
                    boost::match_not_dot_newline
                )) )
    {
        return "ierr";
    }
    // compilation failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Compile).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        return "comp";
    }
    // linking failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Link).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        return "link";
    }
    // run failed
    else if ( ! boost::empty( boost::algorithm::find_regex(
                    log,
                    boost::basic_regex<char>("(Run).+(fail).*$"),
                    boost::match_not_dot_newline
                )) )
    {
        return "run";
    }

    // unknown fail
    return "unkn";
}

std::string filename_from_url(std::string const& url)
{
    return url.substr(url.find_last_of('/') + 1);
}

std::string reason_to_style(std::string const& reason)
{
    if ( reason == "time" )
        return "background-color: #88ff00;";
    else if ( reason == "file" )
        return "background-color: #00ff88;";
    else if ( reason == "ierr" )
        return "background-color: #ff88ff;";
    else if ( reason == "comp")
        return "background-color: #ffbb00;";
    else if ( reason == "link" )
        return "background-color: #ffdd00;";
    else if ( reason == "run" )
        return "background-color: #ffff00;";
    else if ( reason == "unkn" )
        return "background-color: #ffff88;";
    else
        return "";
}

void process_fail(rapidxml::xml_document<> & doc,
                  fail_node & n,
                  std::string const& log,
                  std::string & reason,
                  options const& op)
{
    // remove spaces
    while ( n.td->first_node("") )
    {
        n.td->remove_node(n.td->first_node(""));
    }

    // set new, global href
    n.href->value( doc.allocate_string(n.log_url.c_str()) );

    if ( op.verbose )
        std::cout << "Processing: " << filename_from_url(n.log_url) << std::endl;

    reason = find_reason(log);

    rapidxml::xml_attribute<> * style_attr = doc.allocate_attribute("style", doc.allocate_string(reason_to_style(reason).c_str()));
    n.td->append_attribute(style_attr);

    set_value(doc, n.a, reason.c_str());
}

void process_anchor(rapidxml::xml_document<> & doc, anchor_node & n)
{
    // set new, global href
    n.href->value( doc.allocate_string(n.url.c_str()) );
}

struct fail_info
{
    fail_info() {}

    fail_info(std::string const& runner_,
              std::string const& toolset_,
              std::string const& test_name_,
              std::string const& reason_,
              std::string const& url_)
        : runner(runner_)
        , toolset(toolset_)
        , test_name(test_name_)
        , reason(reason_)
        , url(url_)
    {}

    bool operator<(fail_info const& r) const
    {
        return test_name < r.test_name
                || test_name == r.test_name && ( runner < r.runner
                    || runner == r.runner && toolset < r.toolset );
    }

    std::string runner;
    std::string toolset;
    std::string test_name;
    std::string reason;
    std::string url;

private:
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::make_nvp("runner", runner);
        ar & boost::serialization::make_nvp("toolset", toolset);
        ar & boost::serialization::make_nvp("test", test_name);
        ar & boost::serialization::make_nvp("reason", reason);
    }

    friend class boost::serialization::access;
};

bool is_reason_important(std::string const& reason)
{
    return reason == "comp" || reason == "link" || reason == "run" || reason == "unkn";
}

void process_document(std::string const& library_name,
                      std::string & in,
                      std::string & out,
                      std::set<fail_info> & failures,
                      options const& op)
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
            std::string reason;
            process_fail(doc, *(log_it->fail_it), log_it->log, reason, op);

            if ( op.track_changes || op.save_report || op.send_report )
            {
                // log only "important" errors
                if ( is_reason_important(reason) )
                {
                    failures.insert(fail_info(
                        nodes.runners[log_it->fail_it->toolset_index],
                        nodes.toolsets[log_it->fail_it->toolset_index],
                        log_it->fail_it->test_name,
                        reason,
                        log_it->fail_it->log_url));
                }
            }
        }
    }

    // process anchors
    for ( nodes_containers::anchors_iterator a_it = nodes.non_log_anchors.begin() ;
          a_it != nodes.non_log_anchors.end() ; ++a_it )
    {
        process_anchor(doc, *a_it);
    }

    std::cout << "Saving: " << library_name << std::endl;

    rapidxml::print(std::back_inserter(out), doc);
}

struct library_fail_info
{
    std::string library;
    std::set<fail_info> failures;

private:
    template<class Archive>
    void serialize(Archive & ar, const unsigned int version)
    {
        ar & boost::serialization::make_nvp("library", library);
        ar & boost::serialization::make_nvp("failures", failures);
    }

    friend class boost::serialization::access;
};

struct compared_fail_info
{
    compared_fail_info() {}

    compared_fail_info(std::vector<library_fail_info>::const_iterator const& library_it_,
                       std::set<fail_info>::const_iterator const& fail_it_,
                       std::set<fail_info>::const_iterator const& previous_fail_it_)
        : library_it(library_it_)
        , fail_it(fail_it_)
        , previous_fail_it(previous_fail_it_)
        , is_fail_valid(true)
        , is_previous_valid(true)
    {}

    compared_fail_info(std::vector<library_fail_info>::const_iterator const& library_it_,
                       std::set<fail_info>::const_iterator const& fail_it_)
        : library_it(library_it_)
        , fail_it(fail_it_)
        , is_fail_valid(true)
        , is_previous_valid(false)
    {}

    compared_fail_info(std::vector<library_fail_info>::const_iterator const& library_it_,
                       bool,
                       std::set<fail_info>::const_iterator const& previous_fail_it_)
        : library_it(library_it_)
        , previous_fail_it(previous_fail_it_)
        , is_fail_valid(false)
        , is_previous_valid(true)
    {}

    std::vector<library_fail_info>::const_iterator library_it;
    std::set<fail_info>::const_iterator fail_it;
    std::set<fail_info>::const_iterator previous_fail_it;
    bool is_fail_valid;
    bool is_previous_valid;
};

struct is_same_library
{
    std::string library;
    is_same_library(std::string const& library_) : library(library_) {}
    bool operator()(library_fail_info const& l) const
    {
        return library == l.library;
    }
};

void compare_failures_logs(std::vector<library_fail_info> const& previous_failures,
                           std::vector<library_fail_info> const& current_failures,
                           std::vector<compared_fail_info> & new_errors,
                           std::vector<compared_fail_info> & changed_errors,
                           std::vector<compared_fail_info> & no_longer_errors)
{
    for ( std::vector<library_fail_info>::const_iterator lib_it = current_failures.begin() ;
          lib_it != current_failures.end() ; ++lib_it )
    {
        std::vector<library_fail_info>::const_iterator
            prev_lib_it = std::find_if(previous_failures.begin(),
                                       previous_failures.end(),
                                       is_same_library(lib_it->library));

        // previous log not found - treat failures as new
        if ( prev_lib_it == previous_failures.end() )
        {
            for ( std::set<fail_info>::const_iterator fail_it = lib_it->failures.begin() ;
                  fail_it != lib_it->failures.end() ; ++fail_it )
            {
                new_errors.push_back(compared_fail_info(lib_it, fail_it));
            }
        }
        else
        {
            // for each new fail
            for ( std::set<fail_info>::const_iterator fail_it = lib_it->failures.begin() ;
                  fail_it != lib_it->failures.end() ; ++fail_it )
            {
                // search for the corresponding one from the previous run
                std::set<fail_info>::const_iterator prev_fail_it = prev_lib_it->failures.find(*fail_it);

                // if the failure wasn't found previously
                if ( prev_fail_it == prev_lib_it->failures.end() )
                {
                    // important reason
                    if ( is_reason_important(fail_it->reason) )
                        new_errors.push_back(compared_fail_info(lib_it, fail_it));
                }
                // the failure found
                else
                {
                    // important reason and different than previously
                    if ( is_reason_important(fail_it->reason)
                      && fail_it->reason != prev_fail_it->reason )
                    {
                        changed_errors.push_back(compared_fail_info(lib_it, fail_it, prev_fail_it));
                    }
                }
            }

            // for each fail from previous run
            for ( std::set<fail_info>::const_iterator prev_fail_it = prev_lib_it->failures.begin() ;
                  prev_fail_it != prev_lib_it->failures.end() ; ++prev_fail_it )
            {
                // search for the corresponding one from this run
                std::set<fail_info>::const_iterator fail_it = lib_it->failures.find(*prev_fail_it);

                // if the failure wasn't found - there is no longer an error
                // NOTE: actually non-important errors should be checked here
                // and mentioned in the report (consider e.g. comp->time)
                if ( fail_it == lib_it->failures.end() )
                {
                    // if the reason was important
                    if ( is_reason_important(prev_fail_it->reason) )
                    {
                        no_longer_errors.push_back(compared_fail_info(lib_it, false, prev_fail_it));
                    }
                }
            }
        }
    }
}

void output_errors(std::vector<compared_fail_info> const& errors,
                   std::ostream & os)
{
    typedef std::vector<compared_fail_info>::const_iterator iterator;

    std::string prev_library;
    std::string prev_test;
    for ( iterator it = errors.begin() ; it != errors.end() ; ++it )
    {
        if ( it->library_it->library != prev_library )
        {
            if ( ! prev_test.empty() )
            {
                os << "</table>";
                os << "</div>";
                os << "</div>";
            }
            prev_test.clear();

            os << "<h3>" << it->library_it->library << "</h3>";
        }

        if ( it->fail_it->test_name != prev_test )
        {
            if ( ! prev_test.empty() )
            {
                os << "</table>";
                os << "</div>";
                os << "</div>";
            }
            os << "<div style=\"margin:10px;\">";
            os << "<span style=\"font-weight: bold;\">" << it->fail_it->test_name << "</span>";
            os << "<div style=\"margin:5px;\">";
            os << "<table style=\"border-width: 0px;\">";
        }

        os << "<tr><td>";
        if ( it->is_previous_valid )
        {
            os << "<span style=\"text-decoration: line-through; " << reason_to_style(it->previous_fail_it->reason) << "\">"
               << it->previous_fail_it->reason << "</span>";

            if ( it->is_fail_valid )
                os << "->";
        }
        if ( it->is_fail_valid )
        {
            os << "<span style=\"" << reason_to_style(it->fail_it->reason) << "\">" << it->fail_it->reason << "</span>";
        }

        os << "</td><td>";
        if ( it->is_fail_valid )
            os << "<a href=\"" << it->fail_it->url << "\">" << it->fail_it->toolset << " (" << it->fail_it->runner << ")</a>";
        else if ( it->is_previous_valid )
            os << it->previous_fail_it->toolset << " (" << it->previous_fail_it->runner << ")";
        os << "</td></tr>";

        prev_library = it->library_it->library;
        prev_test = it->fail_it->test_name;
    }

    if ( ! prev_test.empty() )
    {
        os << "</table>";
        os << "</div>";
        os << "</div>";
    }
}

void output_report(std::vector<compared_fail_info> const& new_errors,
                   std::vector<compared_fail_info> const& changed_errors,
                   std::vector<compared_fail_info> const& no_longer_errors,
                   std::ostream & os)
{
    // NOTE: errors should be sorted in the following order:
    // library -> test -> runner -> toolset

    os << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\""
       << " \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
       << "<html xmlns=\"http://www.w3.org/1999/xhtml\">"
       << "<head><title></title>"
       << "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>"
       << "</head><body>";

    os << "<div style=\"margin:10px;\">"
       << new_errors.size() << " new failures."
       << "<br/>"
       << changed_errors.size() << " changed failures."
       << "<br/>"
       << no_longer_errors.size() << " failures dissapeared."
       << "</div>";

    if ( ! new_errors.empty() )
    {
        os << "<h2>New errors:</h2>";
        output_errors(new_errors, os);
    }

    if ( ! changed_errors.empty() )
    {
        os << "<h2>Changed errors:</h2>";
        output_errors(changed_errors, os);
    }

    if ( ! no_longer_errors.empty() )
    {
        os << "<h2>Errors dissapeared:</h2>";
        output_errors(no_longer_errors, os);
    }

    os << "</body></html>";
}

int main(int argc, char **argv)
{
    options op;
    try
    {
        // process program options
        if ( ! process_options(argc, argv, op) )
            return 1;
    }
    catch (std::exception & e)
    {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    // prepare the environment
    try
    {
        // download CSS file if needed
        boost::filesystem::path css_path = "master.css";
        if ( !boost::filesystem::exists(css_path) )
        {
            std::cout << "Downloading style." << std::endl;

            try
            {
                std::string body = get_document("http://www.boost.org/development/tests/develop/master.css");
                std::ofstream of("master.css", std::ios::trunc);
                of << body;
            }
            catch (std::exception & e)
            {
                std::cerr << "Error downloading style: " << e.what() << std::endl;
                std::cerr << "You may try to download it manually from http://www.boost.org/development/tests/develop/master.css and place it in the working directory." << std::endl;
                return 1;
            }
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

    // prepare container for new failures
    std::vector<library_fail_info> failures(op.libraries.size());

    // process all libraries
    for ( std::vector<std::string>::iterator it = op.libraries.begin() ;
          it != op.libraries.end() ; ++it )
    {
        try
        {
            std::string const& lib = *it;
            std::string url = op.view_url + lib + "_.html";

            std::cout << "Downloading: " << lib << std::endl;

            // download the summary page
            std::string body = get_document(url);

            std::cout << "Processing." << std::endl;

            // process the summary page
            std::string processed_body;
            process_document(lib, body, processed_body,
                             failures[std::distance(op.libraries.begin(), it)].failures,
                             op);

            // set library name
            failures[std::distance(op.libraries.begin(), it)].library = lib;

            // save processed summary page
            std::string of_name = std::string("result/") + op.branch + '-' + lib + ".html";
            std::ofstream of(of_name.c_str(), std::ios::trunc);
            of << processed_body;
            of.close();
        }
        catch (std::exception & e)
        {
            std::cerr << "Error: " << e.what() << std::endl;

            failures[std::distance(op.libraries.begin(), it)].library.clear();
            failures[std::distance(op.libraries.begin(), it)].failures.clear();
        }
    }

    std::string failures_log_path = op.log_format == options::xml ? "failures.xml" : "failures.bin";

    // load old failures
    std::vector<library_fail_info> old_failures;
    bool old_failures_opened = false;
    if ( op.track_changes )
    {
        try
        {
            std::ifstream ifs(failures_log_path.c_str(), std::ios::binary);
            if ( ifs.is_open() )
            {
                if ( op.log_format == options::xml )
                {
                    boost::archive::xml_iarchive ia(ifs);
                    ia >> boost::serialization::make_nvp("libraries", old_failures);
                }
                else
                {
                    boost::archive::binary_iarchive ia(ifs);
                    ia >> boost::serialization::make_nvp("libraries", old_failures);
                }
                old_failures_opened = true;
            }

            if ( old_failures_opened )
                std::cout << "Failures log found." << std::endl;
            else
                std::cout << "Failures log not found." << std::endl;
        }
        catch (std::exception & e)
        {
            std::cerr << "Error loading failures log: " << e.what() << std::endl;

            // The log may be corrupted, try to remove it
            boost::system::error_code ec;
            boost::filesystem::remove(failures_log_path, ec); // ignore error
        }
    }

    // NOTE: In case if reports should be emailed
    // new log should be saved only if the email was sent properly
    bool is_safe_to_save_log = true;

    // reporting enabled
    // NOTE: if tracking is disabled all errors will be treated as new
    if ( op.send_report || op.save_report )
    {
        std::vector<compared_fail_info> new_errors;
        std::vector<compared_fail_info> changed_errors;
        std::vector<compared_fail_info> no_longer_errors;
        compare_failures_logs(old_failures, failures, new_errors, changed_errors, no_longer_errors);

        std::stringstream report_stream;
        output_report(new_errors, changed_errors, no_longer_errors, report_stream);

        if ( op.save_report )
        {
            std::cout << "Saving report." << std::endl;
            std::ofstream ofs("report.html", std::ios::trunc);
            ofs << report_stream.str();
        }

        if ( op.send_report && ( !op.track_changes || !new_errors.empty() || !changed_errors.empty() || !no_longer_errors.empty() ) )
        {
            mail::config cfg;
            if ( ! cfg.load("mail.cfg") )
            {
                std::cerr << "Unable to load mailing info." << std::endl;
                is_safe_to_save_log = false;
            }
            else
            {
                std::cout << "Sending report." << std::endl;

                try
                {
                    std::string subject = "Regression report";
                    if ( op.track_changes )
                    {
                        if ( !new_errors.empty() && !changed_errors.empty() )
                            subject = "New and changed errors detected!";
                        else if ( !new_errors.empty() )
                            subject = "New errors detected!";
                        else if ( !changed_errors.empty() )
                            subject = "Changed errors detected!";
                        else if ( !no_longer_errors.empty() )
                            subject = "Errors dissapeared!";
                        else
                            subject = "Errors detected!";
                    }

                    mail::send(cfg, subject, report_stream.str(), true);
                }
                catch (std::exception & e)
                {
                    std::cerr << "Error sending report: " << e.what() << std::endl;
                    is_safe_to_save_log = false;
                }
            }
        }
    }

    if ( op.track_changes && is_safe_to_save_log )
    {
        std::cout << "Saving failures log." << std::endl;

        try
        {
            std::ofstream ofs(failures_log_path.c_str(), std::ios::trunc | std::ios::binary);
            if ( !ofs.is_open() )
                throw std::runtime_error("unable to open file");

            if ( op.log_format == options::xml )
            {
                boost::archive::xml_oarchive oa(ofs);
                oa << boost::serialization::make_nvp("libraries", failures);
            }
            else
            {
                boost::archive::binary_oarchive oa(ofs);
                oa << boost::serialization::make_nvp("libraries", failures);
            }
        }
        catch (std::exception & e)
        {
            std::cerr << "Error saving failures log: " << e.what() << std::endl;
        }
    }

    return 0;
}
