/***
* ==++==
*
* Copyright (c) Microsoft Corporation. All rights reserved. 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
* http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* ==--==
* =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
*
* Oauth1Client.cpp : Defines the entry point for the console application
*
* =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
****/

/*

INSTRUCTIONS

This sample performs authorization on various OAuth 1.0 services and
then requests basic user information.

This sample is for Windows Desktop, OS X and Linux.
Execute with administrator privileges.

Set the app key & secret strings below (i.e. s_linkedin_key, s_linkedin_secret, etc.)
To get key & secret, register an app in the corresponding service.

*/
#include "stdafx.h"

#if defined(_MS_WINDOWS) && !defined(__cplusplus_winrt)
// Extra includes for Windows desktop.
#include <windows.h>
#include <Shellapi.h>
#endif

#include "cpprest/http_listener.h"
#include "cpprest/http_helpers.h"

using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::details;
using namespace web::http::client;
using namespace web::http::experimental::listener; 


// Consumer key and secret pairs.
static const utility::string_t s_linkedin_key(U(""));
static const utility::string_t s_linkedin_secret(U(""));


static void open_browser(utility::string_t auth_uri)
{
#if defined(_MS_WINDOWS) && !defined(__cplusplus_winrt)
    // NOTE: Windows desktop only.
    auto r = ShellExecute(NULL, "open", conversions::utf16_to_utf8(auth_uri).c_str(), NULL, NULL, SW_SHOWNORMAL);
#elif defined(__APPLE__)
    // NOTE: OS X only.
    string_t browser_cmd(U("open \"") + auth_uri + U("\""));
    system(browser_cmd.c_str());
#else
    // NOTE: Linux/X11 only.
    string_t browser_cmd(U("xdg-open \"") + auth_uri + U("\""));
    system(browser_cmd.c_str());
#endif
}


class oauth1_code_listener
{
public:
    oauth1_code_listener(
        uri listen_uri,
        oauth1_config& config) :
            m_listener(utility::details::make_unique<http_listener>(listen_uri)),
            m_config(config)
    {
        m_listener->support([this](http::http_request request) -> void
        {
            pplx::extensibility::scoped_critical_section_t lck(m_resplock);
            m_config.token_from_redirected_uri(request.request_uri()).then([this,request](pplx::task<void> token_task) -> void
            {
                try
                {
                        token_task.wait();
                        request.reply(status_codes::OK, U("Ok."));
                        m_tce.set();
                }
                catch (oauth1_exception& e)
                {
                    ucout << "Error: " << e.what() << std::endl;
                    request.reply(status_codes::NotFound, U("Not found."));
                }
            });
        });
        m_listener->open().wait();
    }

    ~oauth1_code_listener()
    {
        m_listener->close().wait();
    }

    pplx::task<void> listen_for_code()
    {
        return pplx::create_task(m_tce);
    }

private:
    std::unique_ptr<http_listener> m_listener;
    pplx::task_completion_event<void> m_tce;
    oauth1_config& m_config;
    pplx::extensibility::critical_section_t m_resplock;
};


class oauth1_session_sample
{
public:
    oauth1_session_sample(utility::string_t name,
        utility::string_t consumer_key,
        utility::string_t consumer_secret,
        utility::string_t temp_endpoint,
        utility::string_t auth_endpoint,
        utility::string_t token_endpoint,
        utility::string_t callback_uri) :
            m_oauth1_config(consumer_key,
                consumer_secret,
                temp_endpoint,
                auth_endpoint,
                token_endpoint,
                callback_uri,
                oauth1_methods::hmac_sha1),
            m_name(name)
    {
#if defined(_MS_WINDOWS) || defined(__APPLE__)
        m_listener = utility::details::make_unique<oauth1_code_listener>(callback_uri, m_oauth1_config);
#else
        uri_builder ub(callback_uri);
        ub.set_host(U("localhost"));
        m_listener = utility::details::make_unique<oauth1_code_listener>(ub.to_uri(), m_oauth1_config);
#endif
    }

    void run()
    {
        if (is_enabled())
        {
            ucout << "Running " << m_name.c_str() << " session sample..." << std::endl;

            if (!m_oauth1_config.token().is_valid())
            {
                do_authorization().wait();
                m_http_config.set_oauth1(m_oauth1_config);
            }

            run_internal();
        }
        else
        {
            ucout << "Skipped " << m_name.c_str() << " session sample because app key or secret is empty. Please see instructions." << std::endl;
        }
    }

protected:
    virtual void run_internal() = 0;

    pplx::task<void> do_authorization()
    {
        open_browser_auth();
        return m_listener->listen_for_code();
    }

    http_client_config m_http_config;
    oauth1_config m_oauth1_config;

private:
    bool is_enabled() const
    {
        return !m_oauth1_config.consumer_key().empty() && !m_oauth1_config.consumer_secret().empty();
    }

    void open_browser_auth()
    {
        auto auth_uri_task(m_oauth1_config.build_authorization_uri());
        try
        {
            auto auth_uri(auth_uri_task.get());
            ucout << "Opening browser in URI:" << std::endl;
            ucout << auth_uri << std::endl;
            open_browser(auth_uri);
        }
        catch (oauth1_exception &e)
        {
            ucout << "Error: " << e.what() << std::endl;
        }
    }

    utility::string_t m_name;
    std::unique_ptr<oauth1_code_listener> m_listener;
};


class linkedin_session_sample : public oauth1_session_sample
{
public:
    linkedin_session_sample() :
        oauth1_session_sample(U("LinkedIn"),
            s_linkedin_key,
            s_linkedin_secret,
            U("https://api.linkedin.com/uas/oauth/requestToken"),
            U("https://www.linkedin.com/uas/oauth/authenticate"),
            U("https://api.linkedin.com/uas/oauth/accessToken"),
            U("http://localhost:8888/"))
    {
    }

protected:
    void run_internal() override
    {
        http_client api(U("https://api.linkedin.com/v1/people/"), m_http_config);

        ucout << "Requesting user information:" << std::endl;
        ucout << "Information: " << api.request(methods::GET, U("~?format=json")).get().extract_json().get() << std::endl;

#if EXTENSIVE
#endif
    }

};


#ifdef _MS_WINDOWS
int wmain(int argc, wchar_t *argv[])
#else
int main(int argc, char *argv[])
#endif
{
    ucout << "Running oauth1 sample..." << std::endl;

    linkedin_session_sample linkedin;
    linkedin.run();

    ucout << "Done." << std::endl;
    return 0;
}

