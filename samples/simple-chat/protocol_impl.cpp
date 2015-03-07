#include "protocol.hpp"


class _TestServiceNetworkImpl : public TestService
{
public:
	enum class FunctionIds : ::RakNet::ServiceFunctionId
	{
		FUNC_print = 0,
		FUNCTION_COUNT
	};
public:
	_TestServiceNetworkImpl(const ::RakNet::SystemAddress& _address)
		: mForeignTargetAddress(_address)
	{
	}

	virtual void print(RakNet::RakString _test, std::function<void()> _done) override
	{
		auto details = GetServiceDetails();
		::RakNet::BitStream stream;
		::RakNet::detail::SerializationArgs sargs(stream, details.GetRakServicePlugin());
		_BeginCall(stream, ::RakNet::ServiceFunctionId(FunctionIds::FUNC_print));
		_AddArg(sargs, _test);
		_AddArg(sargs, _done);
		_EndCall(stream, mForeignTargetAddress);
	}

	virtual bool _IsForeignService() const override
	{
		return true;
	}


private:
	::RakNet::SystemAddress mForeignTargetAddress;
};

namespace TestService_MetaInfoContent
{
	::RakNet::RakServiceFunctionMetaInfo TestServiceFunctions[] =
	{
		{ ::RakNet::ServiceFunctionId(_TestServiceNetworkImpl::FunctionIds::FUNC_print), "print", "std::string _test, std::function<void()> _done"}
	};

	::RakNet::RakServiceMetaInfo TestServiceMetaInfo = 
	{
		"TestService",
		TestServiceFunctions,
		TestServiceFunctions + ::RakNet::ServiceFunctionId(_TestServiceNetworkImpl::FunctionIds::FUNCTION_COUNT)
	};
}

::RakNet::RakServiceMetaInfo* ::RakNet::GenericRakService<TestService>::MetaInfo()
{
	return &TestService_MetaInfoContent::TestServiceMetaInfo;
}

bool ::RakNet::GenericRakService<TestService>::_Invoke(::RakNet::detail::DeserializationArgs& _stream, ::RakNet::ServiceFunctionId _func)
{
	TestService* myself = static_cast<TestService*>(this);
	typedef ::RakNet::ServiceFunctionId sfid;
	switch (_func)
	{
	case sfid(_TestServiceNetworkImpl::FunctionIds::FUNC_print):
		{
			std::function<void(RakNet::RakString, std::function<void()>)>func = std::bind(&TestService::print, myself, std::placeholders::_1, std::placeholders::_2);
			::RakNet::detail::ExpandCall(func, _stream);
		}break;
	default:
		return false;
	}

	return true;
}

TestService* RakNet::GenericRakService<TestService>::_CreateClientImplementation(const SystemAddress& addr)
{
	return new _TestServiceNetworkImpl(addr);
}