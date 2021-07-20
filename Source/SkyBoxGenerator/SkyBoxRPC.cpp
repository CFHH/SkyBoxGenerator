#include "SkyBoxRPC.h"
#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"


SkyBoxServiceImpl* SkyBoxServiceImpl::ms_instance = NULL;

void SkyBoxServiceImpl::RunServer()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::RunServer()"));
    std::string server_address("0.0.0.0:50051");
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    SkyBoxServiceImpl::Instance()->AddTestJob();  //测试
    builder.RegisterService(SkyBoxServiceImpl::Instance());
    SkyBoxServiceImpl::Instance()->m_grpc_server = builder.BuildAndStart();
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！RPC Server listening on %s"), server_address.c_str());
    SkyBoxServiceImpl::Instance()->m_grpc_server->Wait();
}

void SkyBoxServiceImpl::ShutDownServer()
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::ShutDownServer()"));
    if (ms_instance != NULL)
    {
        ms_instance->m_grpc_server->Shutdown();
        ms_instance->m_jobs_completed.clear();  //测试
        ms_instance->m_key2jobs_completed.clear();  //测试
        ms_instance->m_id2jobs_completed.clear();  //测试
    }
    //grpc::Server::Shutdown();
    //grpc::CompletionQueue::Shutdown();
}

SkyBoxServiceImpl* SkyBoxServiceImpl::Instance()
{
    if (ms_instance == NULL)
        ms_instance = new SkyBoxServiceImpl();
    return ms_instance;
}

SkyBoxServiceImpl::SkyBoxServiceImpl()
{
    m_next_job_id = 1;
}

SkyBoxServiceImpl::~SkyBoxServiceImpl()
{
}

void SkyBoxServiceImpl::AddTestJob()
{
    FScopeLock lock(&m_lock);

    //SkyBoxPosition key1;
    //key1.scene_id = 0;
    //key1.x = -351.0f;
    //key1.y = -99.0f;
    //key1.z = 235.0f;
    //CreateNewJob(&key1);

    SkyBoxPosition key2;
    key2.scene_id = 0;
    key2.x = 329.0f;
    key2.y = -359.0f;
    key2.z = 1000.0f;
    CreateNewJob(&key2);

    SkyBoxPosition key3;
    key3.scene_id = 0;
    key3.x = 100.0f;
    key3.y = 0.0f;
    key3.z = 110.0f;
    CreateNewJob(&key3);
}

grpc::Status SkyBoxServiceImpl::SayHello(grpc::ServerContext* context, const skybox::HelloRequest* request, skybox::HelloReply* reply)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::SayHello(), name = %S"), request->name().c_str());
    std::string prefix("Hello ");
    reply->set_message(prefix + request->name());
    return grpc::Status::OK;
}

grpc::Status SkyBoxServiceImpl::GenerateSkyBox(grpc::ServerContext* context, const skybox::GenerateSkyBoxRequest* request, skybox::GenerateSkyBoxReply* reply)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::GenerateSkyBox(), posotion = (%.1f, %.1f, %.1f)"), request->position().x(), request->position().y(), request->position().z());
    SkyBoxPosition key;
    key.scene_id = 0;
    key.x = request->position().x();
    key.y = request->position().y();
    key.z = request->position().z();
    FScopeLock lock(&m_lock);
    //先从已经完成的里面找
    std::map<SkyBoxPosition, SkyBoxJob*>::iterator itr = m_key2jobs_completed.find(key);
    if (itr != m_key2jobs_completed.end())
    {
        reply->set_job_id(0);
        return grpc::Status::OK;
    }
    //再从排队的中找
    itr = m_key2jobs.find(key);
    if (itr != m_key2jobs.end())
    {
        SkyBoxJob* job = itr->second;
        reply->set_job_id(job->m_id);
        return grpc::Status::OK;
    }
    //创建新的
    SkyBoxJob* job = CreateNewJob(&key);
    reply->set_job_id(job->m_id);
    return grpc::Status::OK;
}

grpc::Status SkyBoxServiceImpl::QueryJob(grpc::ServerContext* context, const skybox::QueryJobRequest* request, skybox::QueryJobReply* reply)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::QueryJob(), job_id = %d"), request->job_id());
    int job_id = request->job_id();
    reply->set_job_id(job_id);
    FScopeLock lock(&m_lock);
    //先从已经完成的里面找
    std::map<int, SkyBoxJob*>::iterator itr = m_id2jobs_completed.find(job_id);
    if (itr != m_id2jobs_completed.end())
    {
        SkyBoxJob* job = itr->second;
        reply->set_job_status(job->m_status);
        return grpc::Status::OK;
    }
    //再从排队的中找
    itr = m_id2jobs.find(job_id);
    if (itr != m_id2jobs.end())
    {
        if (job_id == m_jobs.front()->m_id)
            reply->set_job_status(skybox::JobStatus::Working);
        else
            reply->set_job_status(skybox::JobStatus::Waiting);
        return grpc::Status::OK;
    }
    //找不到的当作完成处理
    reply->set_job_status(skybox::JobStatus::Succeeded);
    return grpc::Status::OK;
}

SkyBoxJob* SkyBoxServiceImpl::GetJob()
{
    FScopeLock lock(&m_lock);
    if (m_jobs.empty())
        return NULL;
    SkyBoxJob* job = m_jobs.front();
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::GetJob(), job_id = %d"), job->m_id);
    return job;
}

void SkyBoxServiceImpl::OnJobCompleted(SkyBoxJob* job)
{
    UE_LOG(LogTemp, Warning, TEXT("！！！！！！！！！！SkyBoxServiceImpl::OnJobCompleted(), job_id = %d"), job->m_id);
    FScopeLock lock(&m_lock);
    if (job == NULL || m_jobs.empty())
        return;
    if (job != m_jobs.front())
        return;
    m_jobs.pop_front();
    m_key2jobs.erase(job->m_position);
    m_id2jobs.erase(job->m_id);
    m_jobs_completed.push_back(job);
    m_key2jobs_completed[job->m_position] = job;
    m_id2jobs_completed[job->m_id] = job;
    //CACHE
    while (m_jobs_completed.size() > m_max_cache_count)
    {
        SkyBoxJob* job2delete = m_jobs_completed.front();
        m_jobs_completed.pop_front();
        m_key2jobs_completed.erase(job2delete->m_position);
        m_id2jobs_completed.erase(job2delete->m_id);
        delete job2delete;
    }
}

int SkyBoxServiceImpl::GenerateJobID()
{
    while (m_id2jobs.find(m_next_job_id) != m_id2jobs.end() || m_id2jobs_completed.find(m_next_job_id) != m_id2jobs_completed.end())
        ++m_next_job_id;
    int id = m_next_job_id;
    ++m_next_job_id;
    return id;
}

SkyBoxJob* SkyBoxServiceImpl::CreateNewJob(SkyBoxPosition* key)
{
    SkyBoxJob* job = new SkyBoxJob();
    job->m_id = SkyBoxServiceImpl::GenerateJobID();
    job->m_position = *key;
    m_jobs.push_back(job);
    m_key2jobs[job->m_position] = job;
    m_id2jobs[job->m_id] = job;
    return job;
}


SkyBoxJob::SkyBoxJob()
{
}

SkyBoxJob::~SkyBoxJob()
{
}