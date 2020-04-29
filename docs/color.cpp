#include <iostream>
#include <cstdio>
#include <cstdlib>

#include <cgicc/CgiDefs.h>
#include <cgicc/Cgicc.h>
#include <cgicc/HTTPHTMLHeader.h>
#include <cgicc/HTMLClasses.h>

int main(int argc, const char *argv[])
{
    try {
        cgicc::Cgicc form_data;
        // cgicc::form_iterator form_iter = form_data.getElement("color");

        std::string color(form_data("color"));
        std::string color_s("color:" + color); 
        std::cout << cgicc::HTTPHTMLHeader() << std::endl;
        std::cout << cgicc::HTMLDoctype(cgicc::HTMLDoctype::eHTML5) << std::endl;

        std::cout << cgicc::html() << cgicc::head(cgicc::title(color)) << std::endl;

        std::cout << cgicc::div().set("style", "width:980px; margin:0 auto; line-height:80px; text-align:center;") << std::endl;
        std::cout << cgicc::h2("Server Demo").set("style", color_s) << std::endl;

        std::cout << cgicc::div() << cgicc::body() << cgicc::html() << std::endl;
    } catch (std::exception &e) {
        fprintf(stderr, "%s", e.what());
    }
    return 0;
}
