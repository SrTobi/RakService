
#include <iostream>
#include <string>
#include <memory>
#include "RakService.hpp"
/*
#define TOKENPASTE2(x, y) x ## y
#define TOKENPASTE(x, y) TOKENPASTE2(x, y)

#ifdef IMPLEMENT_PROTOCOL
#undef PACKET_BEGIN
#undef PACKET_MEMBER
#undef PACKET_END
#define PACKET_BEGIN(_name)
#define PACKET_MEMBER(_name, _type, ...)	
#define PACKET_END()
#else
#define PACKET_BEGIN(_name) class _name { public: inline void out() const {
#define PACKET_MEMBER(_name, _type, ...)	TOKENPASTE(_out_, __LINE__)(); } public: _type _name __VA_ARGS__; private: inline void TOKENPASTE(_out_, __LINE__)() const { std::cout << _name << std::endl;	
#define PACKET_END()	}};
#endif



PACKET_BEGIN(test)

PACKET_MEMBER(x, int)
PACKET_MEMBER(y, (std::string), = "test")

PACKET_END()
*/


#define RAK_SERVICE(_name) struct _name : public RakNet::GenericRakService<_name>
//#define RAK_SLOT(_name, ...) RakNet::ServiceSlot<_name>


RAK_SERVICE(TestService)
{
	//RAK_SLOT(test) back;
	virtual void print(RakNet::RakString _test, std::function<void()> done) = 0;
};


/*
 *
 *	
 *	auto service = new test_impl();
 *
 *	plugin->add_service("test", service);
 *
 *
 *-------------------
 *
 *
 *	auto service = plugin->connect_service<test>("test", address);
 *	service->back += new test_my_impl(service.get());
 *
 **/