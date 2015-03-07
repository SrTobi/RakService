#include <stdexcept>
#include "RakService.hpp"
#include "NetworkIDManager.h"
#include "MessageIdentifiers.h"

namespace RakNet {

	enum class ServiceMessageIds : MessageID
	{
		SMI_CONNECT = 1,
		SMI_RETURN = 2,
		SMI_INVOKE = 3,
		SMI_DETACH = 4
	};


	namespace detail {
		void SerializeService::write(SerializationArgs& args, RakService* _p)
		{
			bool isNull = _p == nullptr;
			args.stream << isNull;

			if (!isNull)
			{
				auto controller = _p->GetServiceController();
				RakAssert(!controller.IsForeignService());
				if (!controller.GetRakServicePlugin())
				{
					args.plugin->IntroduceService(_p);
				}
				RakAssert(args.plugin == controller.GetRakServicePlugin());
				args.stream.Write(controller.GetServiceId());
			}
		}
	}


	RakServicePlugin::RakServicePlugin(char channel)
		: mChannel(channel)
		, mNextReturnSlotId(42)
		, mNextServiceId(2)
	{
	}

	RakServicePlugin::~RakServicePlugin()
	{
	}

	void RakServicePlugin::AddService(const char* name, RakService* service)
	{
		IntroduceService(service);
		mWelcomeServices.emplace(name, service);
	}

	RakService* RakServicePlugin::GetService(const char* name)
	{
		auto it = mWelcomeServices.find(name);
		return it == mWelcomeServices.end() ? nullptr : it->second;
	}

	RakService* RakServicePlugin::RemoveService(const char* name)
	{
		auto it = mWelcomeServices.find(name);
		if (it == mWelcomeServices.end())
			return nullptr;

		auto* service = it->second;
		mWelcomeServices.erase(it);
		return service;
	}

	void RakServicePlugin::IntroduceService(RakService* service)
	{
		auto controller = service->GetServiceController();
		if (controller.GetRakServicePlugin() == this)
			return;
		RakAssert(controller.GetRakServicePlugin() == nullptr);

		service->_mServicePlugin = this;
		service->_mServiceId = mNextServiceId++;

		mServices.emplace(controller.GetServiceId(), service);
	}

	void RakServicePlugin::OnAttach(void)
	{
	}

	void RakServicePlugin::OnDetach(void)
	{
	}

	PluginReceiveResult RakServicePlugin::OnReceive(Packet *packet)
	{
		if(MessageID(packet->data[0]) == ID_RPC_PLUGIN)
		{
			BitStream stream(packet->data + sizeof(MessageID), packet->bitSize - sizeof(MessageID), false);
			_HandlePackage(stream, packet);
			return RR_STOP_PROCESSING_AND_DEALLOCATE;
		}

		return RR_CONTINUE_PROCESSING;
	}

	void RakServicePlugin::OnClosedConnection(const SystemAddress &systemAddress, RakNetGUID rakNetGUID, PI2_LostConnectionReason lostConnectionReason)
	{
	}


	void RakServicePlugin::_ConnectService(const char* name, AddressOrGUID systemIdentifier, std::function<void(detail::DeserializationArgs&)> handler)
	{
		auto slotId = _RegisterReturn(handler);
		BitStream conStream;
		conStream.Write(MessageID(ID_RPC_PLUGIN));
		conStream.Write(MessageID(ServiceMessageIds::SMI_CONNECT));
		conStream.Write(RakNet::RakString(name));
		conStream.Write(slotId);
		
		SendUnified(&conStream, HIGH_PRIORITY, RELIABLE_ORDERED, mChannel, systemIdentifier, false);
	}

	RakServicePlugin::ReturnSlotId RakServicePlugin::_RegisterReturn(ServiceFunctionReturnSlot _callback)
	{
		auto slotId = mNextReturnSlotId++;

		auto ret = mReturnSlots.emplace(slotId, std::move(_callback));
		RakAssert(ret.second);

		return slotId;
	}

	void RakServicePlugin::_BeginReturn(detail::SerializationArgs& sargs, ReturnSlotId rid)
	{
		sargs.stream.Write(MessageID(ID_RPC_PLUGIN));
		sargs.stream.Write(MessageID(ServiceMessageIds::SMI_RETURN));
		sargs.stream.Write(rid);
	}

	void RakServicePlugin::_EndReturn(detail::SerializationArgs& sargs, const SystemAddress& _address)
	{
		SendUnified(&sargs.stream, HIGH_PRIORITY, RELIABLE_ORDERED, mChannel, _address, false);
	}

	void RakServicePlugin::_EndCall(const BitStream& stream, const SystemAddress& _address)
	{
		SendUnified(&stream, HIGH_PRIORITY, RELIABLE_ORDERED, mChannel, _address, false);
	}

	void RakServicePlugin::_HandlePackage(BitStream& _stream, Packet* packet)
	{
		ServiceMessageIds pid = ServiceMessageIds(*_stream.GetData());
		_stream.IgnoreBytes(1);

		switch (pid)
		{
		case ServiceMessageIds::SMI_CONNECT:
			_HandleConnect(_stream, packet);
			break;
		case ServiceMessageIds::SMI_RETURN:
			_HandleReturn(_stream, packet);
			break;
		case ServiceMessageIds::SMI_INVOKE:
			_HandleInvoke(_stream, packet);
			break;
		case ServiceMessageIds::SMI_DETACH:
			break;
		default:
			break;
		}
	}

	void RakServicePlugin::_HandleConnect(BitStream& _stream, Packet* packet)
	{
		RakNet::RakString serviceName;
		_stream.Read(serviceName);

		RakService* service = GetService(serviceName.C_String());

		if (!service)
		{

		}

		service->_mRecvAddress = packet->systemAddress;
		service->OnConnect();
		service->_mRecvAddress = UNASSIGNED_SYSTEM_ADDRESS;

		BitStream retStream;
		detail::DeserializationArgs args(_stream, this, packet->systemAddress);
		std::function<void(RakService*)> retFunc;
		detail::DeserializeFunction::read(args, retFunc);
		retFunc(service);
	}

	void RakServicePlugin::_HandleReturn(BitStream& _stream, Packet* packet)
	{
		ReturnSlotId rid;
		_stream.Read(rid);

		auto it = mReturnSlots.find(rid);
		if (it == mReturnSlots.end())
			return;

		// call function
		detail::DeserializationArgs sargs(_stream, this, packet->systemAddress);
		it->second(sargs);
	}

	void RakServicePlugin::_HandleInvoke(BitStream& _stream, Packet* packet)
	{
		RakServiceId sid;
		_stream.Read(sid);

		auto it = mServices.find(sid);
		if (it != mServices.end())
		{
			auto* service = it->second;
			ServiceFunctionId fid;
			_stream.Read(fid);
			detail::DeserializationArgs sargs(_stream, this, packet->systemAddress);
			service->_mRecvAddress = packet->systemAddress;
			service->_Invoke(sargs, fid);
			service->_mRecvAddress = UNASSIGNED_SYSTEM_ADDRESS;
		}
	}

	class RakServicePlugin::ForeignServiceTable
	{
	public:
		void addService(RakService* service)
		{
			RakAssert(service);
			auto controller = service->GetServiceController();
			auto res = mServices.emplace(controller.GetServiceId(), std::unique_ptr<RakService>(service));
			RakAssert(res.second);
		}

		RakService* getService(RakServiceId sid)
		{
			auto it = mServices.find(sid);
			return it == mServices.end() ? nullptr : it->second.get();
		}

	private:
		std::unordered_map<RakServiceId, std::unique_ptr<RakService>> mServices;
	};

	RakServicePlugin::ForeignServiceTable* RakServicePlugin::_GetForeignServiceTable(const SystemAddress& addr)
	{
		auto it = mForeignServices.find(addr);

		if (it == mForeignServices.end())
		{
			std::unique_ptr<ForeignServiceTable> table(new ForeignServiceTable());
			auto* tablePtr = table.get();
			mForeignServices.emplace_hint(it, addr, std::move(table));

			return tablePtr;
		}

		return it->second.get();
	}

	RakService* RakServicePlugin::_GetForeignService(const SystemAddress& addr, RakServiceId sid)
	{
		return _GetForeignServiceTable(addr)->getService(sid);
	}

	void RakServicePlugin::_AddForeignService(const SystemAddress& addr, RakServiceId sid, RakService* serivce)
	{
		RakAssert(serivce->GetServiceController().GetRakServicePlugin() == nullptr);
		serivce->_mServicePlugin = this;
		serivce->_mServiceId = sid;
		_GetForeignServiceTable(addr)->addService(serivce);
	}

	/************************************** RakService **************************************/

	RakService::~RakService()
	{
	}

	void RakService::OnConnect()
	{
	}

	void RakService::OnDisconnect()
	{
	}

	void RakService::_BeginCall(BitStream& stream, ServiceFunctionId _funcId)
	{
		stream.Write(MessageID(ID_RPC_PLUGIN));
		stream.Write(MessageID(ServiceMessageIds::SMI_INVOKE));
		stream.Write(RakServiceId(_mServiceId));
		stream.Write(_funcId);
	}

	void RakService::_EndCall(const BitStream& _stream, const SystemAddress& _address)
	{
		_mServicePlugin->_EndCall(_stream, _address);
	}

	bool RakService::_IsForeignService() const
	{
		return false;
	}
}
