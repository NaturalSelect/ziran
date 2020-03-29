﻿#include <iostream>
#include <stdx/file.h>
#include <stdx/string.h>
#include <stdx/net/http.h>
#include <stdx/traits/convertable.h>
#include <stdx/net/socket.h>
int main(int argc, char **argv)
{
	setlocale(LC_ALL, "chs");
#define ENABLE_WEB
#ifdef ENABLE_WEB
#pragma region web_test
	stdx::file_io_service file_io_service;
	stdx::file doc(file_io_service,U("./index.html"));
	if (!doc.exist())
	{
		stdx::perrorf(U("Error:File {0} does not exist"),doc.path());
		return -1;
	}
	auto stream = doc.open_stream(stdx::file_access_type::read, stdx::file_open_type::open);
	std::string doc_content;
	stream.read_to_end(0).then([&doc_content](stdx::task_result<stdx::file_read_event> r) mutable
	{
		try
		{
			auto &&e = r.get();
			doc_content = std::string(e.buffer, e.buffer.size());
		}
		catch (const std::exception &err)
		{
			stdx::perrorf(U("Error:{0}"),err.what());
			std::terminate();
		}
	}).wait();
	stdx::network_io_service service;
	stdx::socket s = stdx::open_socket(service, stdx::addr_family::ip, stdx::socket_type::stream, stdx::protocol::tcp);
	try
	{
		stdx::ipv4_addr addr(U("127.0.0.1"), 8080);
		s.bind(addr);
		stdx::printf(U("Listen: {0}:{1}\n"),addr.ip(),addr.port());
	}
	catch (std::exception &e)
	{
		stdx::perrorf(U("Error:{0}\n"), e.what());
		return -1;
	}
	s.listen(65535);
	while (true)
	{
		auto c = s.accept();
		auto t = c.recv(1024).then([c,doc_content](stdx::network_recv_event e) mutable
		{
			std::string tmp(e.buffer,e.size);
			try
			{
				stdx::http_request&& request = stdx::http_request::from_bytes(tmp);
				stdx::printf(U("HTTP-Version:{0}\nUrl:{1}\nMethod:{2}\n"),stdx::http_version_string(request.header().version()), request.request_header().request_url(),stdx::http_method_string(request.request_header().method()));
				stdx::printf(U("Headers:\n"));
				for (auto begin = request.request_header().begin(),end=request.request_header().end();begin!=end;begin++)
				{
					stdx::printf(U("	{0}:{1}\n"),begin->first,begin->second);
				}
				if (!request.request_header().cookies().empty())
				{
					stdx::printf(U("Cookies:\n"));
					for (auto begin = request.request_header().cookies().begin(),end= request.request_header().cookies().end();begin!=end;begin++)
					{
						stdx::printf(U("	{0}:{1}\n"),begin->name(),begin->value());
					}
				}
				if (request.request_header().request_url() == U("/"))
				{
					stdx::http_response response(200);
					response.response_header().add_header(U("Content-Type"), U("text/html"));
					std::string body = stdx::ansi_to_utf8(doc_content);
					const unsigned long long int& tmp = body.size();
					response.response_header().add_header(U("Content-Length"), stdx::to_string(tmp));
					response.response_body().push((const unsigned char *)body.c_str(), body.size());
					return response;
				}
				else if (request.request_header().request_url() == U("/test"))
				{
					stdx::string val;
					if (request.form().form_type() == stdx::http_form_type::multipart )
					{
						val = request.form().get(U("test")).map_body_as_string();
					}
					else
					{
						val = request.form().get(U("test")).val_as_string();
					}
					stdx::printf(U("Form Value:{0}\n"),val);
					stdx::http_response response(200);
					response.response_header().add_header(U("Content-Type"), U("text/html"));
					std::string body = stdx::ansi_to_utf8(doc_content);
					const unsigned long long int& tmp = body.size();
					response.response_header().add_header(U("Content-Length"), stdx::to_string(tmp));
					response.response_body().push((const unsigned char*)body.c_str(), body.size());
					return response;
				}
				else
				{
					stdx::http_response response(404);
					response.response_header().add_header(U("Content-Type"), U("text/html"));
					stdx::string body = U("<html><body><h1>Not Found</h1></body></html>");
					response.response_body().push(body);
					return response;
				}
			}
			catch (const std::exception&err)
			{
				stdx::perrorf(U("Error:{0}\n"), err.what());
			}
		}).then([c](stdx::http_response res) mutable
		{
			std::vector<unsigned char>&& bytes = res.to_bytes();
			stdx::uint64_union u;
			u.value = bytes.size();
			auto t = c.send((const char*)bytes.data(), u.low);
			return t;
		})
		.then([c](stdx::task_result<stdx::network_send_event> r) mutable
		{
			try
			{
				auto e = r.get();
			}
			catch (const std::exception& err)
			{
				stdx::perrorf(U("Error:{0}\n"), err.what());
			}
		});
	}
#pragma endregion
#endif
	return 0;
}