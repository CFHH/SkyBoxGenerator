#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <list>
#include "CoreMinimal.h"
#pragma warning (push)
#pragma warning (disable : 4800)
#pragma warning (disable : 4125)
#pragma warning (disable : 4647)
#pragma warning (disable : 4668)
#pragma warning (disable : 4582)
#pragma warning (disable : 4583)
#pragma warning (disable : 4946)
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include "./protos/skybox.pb.h"
#include "./protos/skybox.grpc.pb.h"
#pragma warning( pop )


struct SkyBoxPosition;
class SkyBoxJob;
class SkyBoxServiceImpl final : public skybox::SkyBoxService::Service
{
public:
    static void RunServer();
    static void ShutDownServer();
    static SkyBoxServiceImpl* Instance();
private:
    SkyBoxServiceImpl();
    static SkyBoxServiceImpl* ms_instance;
    std::unique_ptr<grpc::Server> m_grpc_server;
public:
    ~SkyBoxServiceImpl();
    void AddTestJob();
    grpc::Status SayHello(grpc::ServerContext* context, const skybox::HelloRequest* request, skybox::HelloReply* reply) override;
    grpc::Status GenerateSkyBox(grpc::ServerContext* context, const skybox::GenerateSkyBoxRequest* request, skybox::GenerateSkyBoxReply* reply) override;
    grpc::Status QueryJob(grpc::ServerContext* context, const skybox::QueryJobRequest* request, skybox::QueryJobReply* reply) override;
    SkyBoxJob* GetJob();
    void OnJobCompleted(SkyBoxJob* job);
private:
    int GenerateJobID();
    SkyBoxJob* CreateNewJob(SkyBoxPosition* key);
private:
    int m_next_job_id;
    FCriticalSection m_lock;
    std::list<SkyBoxJob*> m_jobs;
    std::map<SkyBoxPosition, SkyBoxJob*> m_key2jobs;
    std::map<int, SkyBoxJob*> m_id2jobs;
private:
    static const int m_max_cache_count = 1000;
    std::list<SkyBoxJob*> m_jobs_completed;
    std::map<SkyBoxPosition, SkyBoxJob*> m_key2jobs_completed;
    std::map<int, SkyBoxJob*> m_id2jobs_completed;
};


struct SkyBoxPosition
{
    int scene_id;
    float x;
    float y;
    float z;
    bool operator < (const SkyBoxPosition& rhs) const
    {
        if (this->scene_id < rhs.scene_id)
            return true;
        if (this->scene_id > rhs.scene_id)
            return false;
        if (this->x < rhs.x)
            return true;
        if (this->x > rhs.x)
            return false;
        if (this->y < rhs.y)
            return true;
        if (this->y > rhs.y)
            return false;
        return this->z < rhs.z;
    }
};


class SkyBoxJob
{
public:
    SkyBoxJob();
    ~SkyBoxJob();
    int JobID() { return m_id; }
    void SetStatus(skybox::JobStatus status) { m_status = status; }
public:
    int m_id;
    SkyBoxPosition m_position;
    skybox::JobStatus m_status;
};


/*
一、在所有protobuf生成的*.pb.cc文件开头，以及包含*.pb.h之前加上

#pragma warning (push)
#pragma warning (disable : 4800)
#pragma warning (disable : 4125)
#pragma warning (disable : 4647)
#pragma warning (disable : 4668)
#pragma warning (disable : 4582)
#pragma warning (disable : 4583)
#pragma warning (disable : 4946)

二、在所有protobuf生成的*.pb.cc文件末尾，以及包含*.pb.h之后加上

#pragma warning(pop)

*/


/*
一、在include任何grpc相关头文件（包括生成的*.pb.cc）之前加上

#if (defined _WIN64) || (defined _WIN32)
#pragma warning (push)
#pragma warning (disable : 4800)
#pragma warning (disable : 4125)
#pragma warning (disable : 4647)
#pragma warning (disable : 4668)
#pragma warning (disable : 4582)
#pragma warning (disable : 4583)
#pragma warning (disable : 4946)
static void MemoryBarrier() {}
#define  GOOGLE_PROTOBUF_NO_RTTI true
#include "AllowWindowsPlatformTypes.h"
#pragma intrinsic(_InterlockedCompareExchange64)
#define InterlockedCompareExchangeAcquire64 _InterlockedCompareExchange64
#define InterlockedCompareExchangeRelease64 _InterlockedCompareExchange64
#define InterlockedCompareExchangeNoFence64 _InterlockedCompareExchange64
#define InterlockedCompareExchange64 _InterlockedCompareExchange64
#endif

二、在以上include之后加上

#if (defined _WIN64) || (defined _WIN32)
#pragma warning( pop )
#include "HideWindowsPlatformTypes.h"
#endif
*/
