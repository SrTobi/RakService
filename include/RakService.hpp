#pragma once
#ifndef _RAKNET_RAKSERVICE_HPP
#define _RAKNET_RAKSERVICE_HPP


#include <functional>
#include <memory>
#include <unordered_map>
#include <tuple>

#include "PluginInterface2.h"
#include "BitStream.h"

namespace RakNet {

	class RakService;
	class RakServicePlugin;
	class NetworkIDManager;
	typedef unsigned char ServiceFunctionId;
	typedef unsigned short RakServiceId;

	namespace detail {

		typedef unsigned short ReturnSlotId;

		template <typename Iterator>
		class iterator_pair {
		public:
			iterator_pair ( Iterator first, Iterator last ) : f_ (first), l_ (last) {}
			Iterator begin () const { return f_; }
			Iterator end   () const { return l_; }

		private:
			Iterator f_;
			Iterator l_;
		};

		template<typename Test, template<typename...> class Ref>
		struct is_specialization : std::false_type {};

		template<template<typename...> class Ref, typename... Args>
		struct is_specialization<Ref<Args...>, Ref> : std::true_type{};

		struct SerializationArgs
		{
			SerializationArgs(BitStream& _stream, RakServicePlugin* _plugin)
				: stream(_stream)
				, plugin(_plugin)
			{}
			BitStream& stream;
			RakServicePlugin* plugin;
		};

		struct DeserializationArgs
		{
			DeserializationArgs(BitStream& _stream, RakServicePlugin* _plugin, const SystemAddress& _addr)
				: stream(_stream)
				, plugin(_plugin)
				, recvAddress(_addr)
			{}
			BitStream& stream;
			RakServicePlugin* plugin;
			const SystemAddress& recvAddress;
		};

		template<int I, typename... Signature>
		struct Expander
		{
		private:
			struct ProceedExpanding
			{
				template<typename Handler, typename... Args>
				static void ExpandCall(const Handler& func, DeserializationArgs& deArgs, Args&&... args)
				{
					typedef typename std::tuple_element<I, std::tuple<Signature...>>::type arg_type;
					arg_type arg;
					Deserializer<arg_type>::type::read(deArgs, arg);

					return Expander<I + 1, Signature...>::type::ExpandCall(func, deArgs, std::forward<Args>(args)..., arg);
				}
			};

			struct DoneExpanding
			{
				static_assert(I <= sizeof...(Signature), "Invalid value for I");

				template<typename Handler, typename... Args>
				static void ExpandCall(const Handler& func, DeserializationArgs& deArgs, Args&&... args)
				{
					func(std::forward<Args>(args)...);
				}
			};

	 	public:
			typedef typename std::conditional<
				(I < sizeof...(Signature)),
				ProceedExpanding,
				DoneExpanding>::type type;
		};

		template<typename... Sig>
		void ExpandCall(void(*func)(Sig...), DeserializationArgs& deArgs)
		{
			Expander<0, Sig...>::type::ExpandCall(func, deArgs);
		}

		template<typename... Sig>
		void ExpandCall(const std::function<void(Sig...)>& func, DeserializationArgs& deArgs)
		{
			Expander<0, Sig...>::type::ExpandCall(func, deArgs);
		}


		template<typename... Sig>
		static std::function<void(DeserializationArgs&)> WrapFunction(std::function<void(Sig...)> func)
		{
			return [func](DeserializationArgs& deArgs)
			{
				ExpandCall(func, deArgs);
			};
		}

		static void PackCall(SerializationArgs&)
		{
		}

		template<typename Arg, typename... Args>
		static void PackCall(SerializationArgs& sa, Arg&& arg, Args&&... tailArgs)
		{
			Serializer<Arg>::type::write(sa, arg);
			PackCall(sa, std::forward<Args>(tailArgs)...);
		}


		template<typename... Sig>
		static std::function<void(Sig...)> MakeInkoation(RakServicePlugin* plugin, ReturnSlotId rid, const SystemAddress& addr)
		{
			return[plugin, rid, addr](Sig... fargs)
			{
				BitStream stream;
				SerializationArgs args(stream, plugin);
				plugin->_BeginReturn(args, rid);
				PackCall(args, std::forward<Sig>(fargs)...);
				plugin->_EndReturn(args, addr);
			};
		}

		struct SerializeFunction
		{
			template<typename... Sig>
			static void write(SerializationArgs& args, const std::function<void(Sig...)>& _func)
			{
				args.stream.Write(args.plugin->_RegisterReturn(WrapFunction(_func)));
			}
		};

		struct SerializeEverything
		{
			template<typename T>
			static void write(SerializationArgs& args, const T& _val)
			{
				args.stream.Write(_val);
			}
		};

		struct SerializeService
		{
			static void write(SerializationArgs& args, RakService* _p);
		};

		template<typename T>
		struct Serializer
		{
		private:
			typedef typename std::conditional <
				is_specialization<T, std::function>::value,
				SerializeFunction,
				SerializeEverything
			>::type type1;

			typedef typename std::conditional <
				std::is_base_of<RakService, typename std::remove_pointer<T>::type>::value,
				SerializeService,
				type1
			> ::type type2;
		public:
			typedef type2 type;
		};

		struct DeserializeFunction
		{
			template<typename... Args>
			static void read(DeserializationArgs& args, std::function<void(Args...)>& _func)
			{
				ReturnSlotId rid;
				args.stream.Read(rid);
				_func = MakeInkoation<Args...>(args.plugin, rid, args.recvAddress);
			}
		};

		struct DeserializeEverything
		{
			template<typename T>
			static void read(DeserializationArgs& args, T& _val)
			{
				args.stream >> _val;
			}
		};

		struct DeserializeService
		{
			template<typename T>
			static void read(DeserializationArgs& args, T*& _p)
			{
				static_assert(std::is_base_of<RakService, T>::value, "_p must be a RakService!");

				bool isNull;
				args.stream >> isNull;
				if (isNull)
				{
					_p = nullptr;
					return;
				}

				RakServiceId sid;
				args.stream.Read(sid);
				_p = args.plugin->GetForeignService<T>(args.recvAddress, sid);
			}
		};

		template<typename T>
		struct Deserializer
		{
		private:
			typedef typename std::conditional <
				is_specialization<T, std::function>::value,
				DeserializeFunction,
				DeserializeEverything
			>::type type1;

			typedef typename std::conditional <
				std::is_base_of<RakService, typename std::remove_pointer<T>::type>::value,
				DeserializeService,
				type1
			> ::type type2;
		public:
			typedef type2 type;
		};

		struct SystemAddressHash
		{
			inline std::size_t operator()(const SystemAddress& _addr)
			{
				std::hash<USHORT> usHash;
				std::hash<u_long> longHash;
				return usHash(_addr.address.addr4.sin_port)
					+ longHash(_addr.address.addr4.sin_addr.s_addr);
			}
		};
	}


	class RakServicePlugin	: public PluginInterface2
	{
		class ForeignServiceTable;
	public:
		typedef std::function<void(detail::DeserializationArgs&)> ServiceFunctionReturnSlot;
		typedef detail::ReturnSlotId ReturnSlotId;
	public:
		RakServicePlugin(NetworkIDManager* idManager, char channel = 0);
		virtual ~RakServicePlugin();

		void AddService(const char* name, RakService* service);
		RakService* GetService(const char* name);
		RakService* RemoveService(const char* name);

		void IntroduceService(RakService* service);
		template<typename Service>
		Service* GetForeignService(const SystemAddress& addr, RakServiceId sid)
		{
			RakService* gservice = _GetForeignService(addr, sid);

			if (gservice)
			{
				Service* service = dynamic_cast<Service*>(gservice);
				RakAssert(service);
				return service;
			}

			Service* service = GenericRakService<Service>::_CreateClientImplementation(addr);
			_AddForeignService(addr, sid, service);
			return service;
		}

		inline NetworkIDManager* GetNetworkIdManager() { return mIdManager; }
		
		template<typename ServiceType>
		void ConnectService(const char* name, AddressOrGUID systemIdentifier, std::function<void(ServiceType*)> handler)
		{
			_ConnectService(name, systemIdentifier, detail::WrapFunction(handler));
		}

		ReturnSlotId _RegisterReturn(ServiceFunctionReturnSlot _callback);
		void _BeginReturn(detail::SerializationArgs&, ReturnSlotId rid);
		void _EndReturn(detail::SerializationArgs&, const SystemAddress& _address);
		void _EndCall(const BitStream& stream, const SystemAddress& _address);
	public:
		// Handle Plugin stuff
		virtual void OnAttach(void) override;
		virtual void OnDetach(void) override;
		virtual PluginReceiveResult OnReceive(Packet *packet) override;
		virtual void OnClosedConnection(const SystemAddress &systemAddress, RakNetGUID rakNetGUID, PI2_LostConnectionReason lostConnectionReason) override;

	private:

		void _ConnectService(const char* name, AddressOrGUID systemIdentifier, ServiceFunctionReturnSlot handler);
		void _HandlePackage(BitStream& _stream, Packet* packet);
		void _HandleConnect(BitStream& _stream, Packet* packet);
		void _HandleReturn(BitStream& _stream, Packet* packet);
		void _HandleInvoke(BitStream& _stream, Packet* packet);
		ForeignServiceTable*_GetForeignServiceTable(const SystemAddress& addr);
		RakService* _GetForeignService(const SystemAddress& addr, RakServiceId sid);
		void _AddForeignService(const SystemAddress& addr, RakServiceId sid, RakService* serivce);
	private:
		NetworkIDManager* mIdManager;
		const char mChannel;
		ReturnSlotId mNextReturnSlotId;
		RakServiceId mNextServiceId;
		std::unordered_map<ReturnSlotId, ServiceFunctionReturnSlot> mReturnSlots;
		std::unordered_map<std::string, RakService*> mWelcomeServices;
		std::unordered_map<RakServiceId, RakService*> mServices;
		std::unordered_map<SystemAddress, std::unique_ptr<ForeignServiceTable>, detail::SystemAddressHash> mForeignServices;
	};

	class RakServiceFunctionMetaInfo
	{
	public:
		inline RakServiceFunctionMetaInfo(ServiceFunctionId _id, const char* _name, const char* _signatur)
			: mName(_name)
			, mId(_id)
			, mSignatur(_signatur)
		{
		}

		inline const char* name() const { return mName; }
		inline const char* signatur() const { return mSignatur; }
		inline const ServiceFunctionId id() const { return mId; }
		
	private:
		const ServiceFunctionId mId;
		const char* mName;
		const char* mSignatur;
	};

	class RakServiceMetaInfo
	{
	public:
		inline RakServiceMetaInfo(const char* _name, const RakServiceFunctionMetaInfo* _begin, RakServiceFunctionMetaInfo* _end)
			: mName(_name)
			, mBeginFunctions(_begin)
			, mEndFunctions(_end)
		{
		}

		inline const char* name() const { return mName; }
		const RakServiceFunctionMetaInfo* function(ServiceFunctionId _id) const;
		inline detail::iterator_pair<const RakServiceFunctionMetaInfo*> functions() const
		{
			return{ mBeginFunctions, mEndFunctions };
		}

	private:
		const char* mName;
		const RakServiceFunctionMetaInfo* mBeginFunctions;
		const RakServiceFunctionMetaInfo* mEndFunctions;
	};

	class RakService
	{
		friend class RakServicePlugin;
	public:
		RakService();
		virtual ~RakService();

		virtual bool isForeign() const;
		virtual RakServiceMetaInfo* metaInfo() const = 0;
		inline RakServiceId serviceId() const { return mServiceId; }
		inline RakServicePlugin* plugin() const { return mServicePlugin; }
	protected:
		void _BeginCall(BitStream& stream, ServiceFunctionId _funcId);
		template<typename T>
		void _AddArg(detail::SerializationArgs& sargs, const T& _arg)
		{
			detail::Serializer<T>::type::write(sargs, _arg);
		}
		void _EndCall(const BitStream& _stream, const SystemAddress& _address);
		virtual bool _Invoke(detail::DeserializationArgs& _stream, ServiceFunctionId _func) = 0;

	private:
		RakServicePlugin* mServicePlugin;
		RakServiceId mServiceId;
	};

	template<typename ServiceType>
	class GenericRakService : public RakService
	{
		friend class RakServicePlugin;
	public:
		static RakServiceMetaInfo* MetaInfo();
		inline virtual RakServiceMetaInfo* metaInfo() const override { return MetaInfo(); }
	protected:
		virtual bool _Invoke(detail::DeserializationArgs& _stream, ServiceFunctionId _func) override;

	private:
		static ServiceType* _CreateClientImplementation(const SystemAddress& addr);

	};
}

#endif