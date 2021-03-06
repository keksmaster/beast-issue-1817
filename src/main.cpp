#include <boost/beast/http.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/verify_context.hpp>
#include <boost/asio/connect.hpp>
#include <memory>
#include <set>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace ssl = net::ssl;
using tcp = net::ip::tcp;

const std::string HttpsScheme = "https";
const std::string LoopbackIpAddress = "127.0.0.1";
constexpr unsigned short osChoosesPort = 0;

enum class SslVersion
{
    TlsV1_3,
    TlsV1_2,
};

std::ostream &
operator<<(
    std::ostream &os,
    SslVersion v)
{
    switch (v)
    {
    case SslVersion::TlsV1_3:os << "SslVersion::TlsV1_3";
        break;
    case SslVersion::TlsV1_2:os << "SslVersion::TlsV1_2";
        break;
    default:os << "invalid_ssl_version";
        break;
    }
    return os;
}

using SupportedSslVersions = std::set<SslVersion>;

void
configureSupportedSslVersions(
    ssl::context &sslContext,
    const SupportedSslVersions &supportedVersions)
{
    sslContext.set_options(ssl::context::no_sslv2);
    sslContext.set_options(ssl::context::no_sslv3);
    sslContext.set_options(ssl::context::no_tlsv1);
    sslContext.set_options(ssl::context::no_tlsv1_1);
    if (supportedVersions.find(SslVersion::TlsV1_2) == supportedVersions.end())
    {
        sslContext.set_options(ssl::context::no_tlsv1_2);
    }
    if (supportedVersions.find(SslVersion::TlsV1_3) == supportedVersions.end())
    {
        sslContext.set_options(ssl::context::no_tlsv1_3);
    }
}

ssl::context
createSslContext(
    const ssl::context_base::method &baseContext,
    const SupportedSslVersions &sslVersions)
{
    boost::asio::ssl::context sslContext(baseContext);
    configureSupportedSslVersions(sslContext, sslVersions);
    sslContext.set_verify_mode(ssl::verify_peer);

    // configure your certificate and private key here
//    sslContext.use_certificate(boost::asio::const_buffer(certStr.c_str(), certStr.length()), sslContext.pem);
//    sslContext.use_private_key(boost::asio::const_buffer(privStr.c_str(), privStr.length()), sslContext.pem);

    return sslContext;
}

class TestClient
{
public:

    explicit TestClient(SupportedSslVersions sslVersions = {SslVersion::TlsV1_3, SslVersion::TlsV1_2})
        : m_sslContext(createSslContext(ssl::context::tls_client, sslVersions))
    {}

    void
    tryHandshake(
        const std::string &host,
        const std::string &port)
    {
        net::io_context ioc;
        ssl::stream<tcp::socket> stream{ioc, m_sslContext};
        const auto addresses = resolveHost(ioc, host, port);
        beast::error_code ec;
        connect(stream, addresses, ec);
        std::cout << "client:\n handshake returned error code=" << ec.value() << "\nerror message=" << ec.message()
                  << "\n";
    }

private:
    ssl::context m_sslContext;

    net::ip::basic_resolver_results<tcp>
    resolveHost(
        net::io_context &ioc,
        const std::string &host,
        const std::string &port)
    {
        tcp::resolver resolver{ioc};
        return resolver.resolve(host, port);
    }


    void
    connect(
        ssl::stream<tcp::socket> &stream,
        const net::ip::basic_resolver_results<tcp> &results,
        beast::error_code &ec)
    {
        net::connect(stream.next_layer(), results.begin(), results.end());
        stream.handshake(ssl::stream_base::client, ec);
    }
};

class TestDevice
{
public:
    explicit TestDevice(const SupportedSslVersions &sslVersions = {SslVersion::TlsV1_3, SslVersion::TlsV1_2}) :
        m_ioc()
        , m_ctx(createSslContext(ssl::context::tls_server, sslVersions))
        , m_acceptor(tcp::acceptor(m_ioc, tcp::endpoint(net::ip::make_address(LoopbackIpAddress), osChoosesPort)))
        , m_socket(tcp::socket(m_ioc))
    {
        accept();
    }

    void
    run()
    {
        m_ioc.run();
    }

    unsigned short
    assignedPort()
    {
        return m_acceptor.local_endpoint().port();
    }

    ~TestDevice()
    {
        m_ioc.stop();
    }

private:
    void
    accept()
    {
        // Clean up any previous connection.
        beast::error_code ec;
        m_socket.close(ec);
        m_acceptor.async_accept(
            m_socket,
            [this](beast::error_code ec) {
                if (ec)
                {
                    accept();
                }
                else
                {
                    ssl::stream<tcp::socket &> stream(m_socket, m_ctx);
                    stream.set_verify_callback([](
                        bool /*preverify_ok*/,
                        ssl::verify_context & /*ctx*/) { return false; });
                    stream.handshake(ssl::stream_base::server, ec);
                    std::cout << "server:\n handshake returned error code=" << ec.value() << "\nerror message="
                              << ec.message() << "\n";
                }
            });
    }

    net::io_context m_ioc;
    ssl::context m_ctx;
    tcp::acceptor m_acceptor;
    tcp::socket m_socket;
};

void
run_test(SslVersion version)
{
    std::cout << "Testing with: " << version << std::endl;

    auto supported = SupportedSslVersions({version});

    TestClient client(supported);
    TestDevice testDevice(supported);

    std::thread t(&TestDevice::run, &testDevice);

    client.tryHandshake(LoopbackIpAddress, std::to_string(testDevice.assignedPort()));

    if (t.joinable())
        t.join();
}

}

int
main(
    int /*argc*/,
    char ** /*argv*/)
{
    std::cout << "OpenSSL Version: " OPENSSL_VERSION_TEXT "\n";
    std::cout << "Boost Version: " << (BOOST_VERSION / 100000) << '.' << (BOOST_VERSION / 100 % 1000) << "."
              << (BOOST_VERSION % 100) << "\n\n";
    run_test(SslVersion::TlsV1_2);
    std::cout << '\n';
    run_test(SslVersion::TlsV1_3);
}
