#include <stdx/net/http_connection.h>

stdx::basic_http_connection::basic_http_connection(const stdx::socket& sock, uint64_t max_size)
	:base_t(sock)
	,m_parser(max_size)
	,m_read_buf(stdx::make_buffer(4096))
{}

void stdx::basic_http_connection::_Read(std::function<void(stdx::http_request, std::exception_ptr)> callback)
{
	if (m_parser.error())
	{
		callback(stdx::http_request(), std::make_exception_ptr(std::make_exception_ptr(stdx::parse_error("parse fault"))));
		return;
	}
	else if (m_parser.finish_count())
	{
		callback(m_parser.pop(), nullptr);
		return;
	}
	auto parser = m_parser;
	stdx::cancel_token token;
	stdx::socket sock = m_socket;
	stdx::buffer buf = m_read_buf;
	m_socket.recv_until(buf, token, [callback,token, parser](stdx::network_recv_event ev) mutable
	{
			parser.push(ev.buffer, ev.size);
			if (!parser.error())
			{
				if (parser.finish_count())
				{
					callback(parser.pop(), nullptr);
					token.cancel();
				}
			}
			else
			{
				callback(stdx::http_request(), std::make_exception_ptr(std::make_exception_ptr(stdx::parse_error("parse fault"))));
			}
	}, [token,callback,sock](std::exception_ptr err) mutable
	{
			token.cancel();
			callback(stdx::http_request(),err);
	});
}

stdx::task<stdx::http_request> stdx::basic_http_connection::read()
{
	stdx::task_completion_event<stdx::http_request> ce;
	_Read([ce](stdx::http_request req,std::exception_ptr err) mutable
	{
			if (err)
			{
				ce.set_exception(err);
				ce.run_on_this_thread();
			}
			else
			{
				ce.set_value(req);
				ce.run_on_this_thread();
			}
	});
	auto t = ce.get_task();
	return t;
}

stdx::task<size_t> stdx::basic_http_connection::write(const stdx::http_response& package)
{
	auto vec = std::move(package.to_bytes());
	stdx::buffer buf = stdx::make_buffer(vec.size());
	for (size_t i = 0; i < vec.size(); i++)
	{
		buf[i] = vec[i];
	}
	auto t = base_t::write(buf, vec.size());
	return t;
}

void stdx::basic_http_connection::read_until(stdx::cancel_token token, std::function<void(stdx::http_request)> fn, std::function<void(std::exception_ptr)> err_handler)
{
	if (token.is_cancel())
	{
		return;
	}
	_Read([token,fn,err_handler,this](stdx::http_request req,std::exception_ptr err) mutable
	{
		if (err)
		{
			err_handler(err);
		}
		else
		{
			try
			{
				fn(req);
			}
			catch (const std::exception&)
			{
				err_handler(std::current_exception());
			}
		}
		if (!token.is_cancel())
		{
			read_until(token, fn, err_handler);
		}
	});
}

stdx::http_connection stdx::make_http_connection(const stdx::socket& sock, uint64_t max_size)
{
	return stdx::make_connection<stdx::basic_http_connection>(sock,max_size);
}