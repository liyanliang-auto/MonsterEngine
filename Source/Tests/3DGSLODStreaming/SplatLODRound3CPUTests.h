#pragma once
inline void RunRound3CPU(Suite& suite){
    suite.Test("R1 T06 accepted scale and disabled distance culling preserve support",[]{
        auto m=SmallTree();m.nodes.resize(1);m.pages.resize(1);m.nodes[0].children.clear();m.nodes[0].pageIds={0};m.nodes[0].gaussianCount=1;
        m.nodes[0].bounds={{5.7f,-.3f,-5.3f},{6.3f,.3f,-4.7f}};
        auto resident=Ready(m);auto view=View();auto settings=Settings();FSplatLODSelector selector;
        Require(selector.Select(m,view,settings,resident).activeCount==0,"scale1 support unexpectedly intersects viewport");
        view.gaussianScale=10;Require(selector.Select(m,view,settings,resident).activeCount==1,"accepted scale10 support omitted");
        view.gaussianScale=1;m.nodes[0].bounds={{-.3f,-.3f,-5.3f},{.3f,.3f,-4.7f}};view.farPlane=4;
        Require(selector.Select(m,view,settings,resident).activeCount==0,"enabled far culling ignored");
        view.distanceCulling=false;Require(selector.Select(m,view,settings,resident).activeCount==1,"disabled renderer distance culling ignored");
        m.nodes[0].bounds={{-.3f,-.3f,4.7f},{.3f,.3f,5.3f}};
        Require(selector.Select(m,view,settings,resident).activeCount==0,"behind-eye support accepted with clipping disabled");
    });
    suite.Test("R1 T03 builder and manifest reject support below renderer three sigma",[]{
        Context c("support-convention");FSplatLODBuilder builder;auto cfg=Config();FSplatSceneManifest m;std::string error;
        for(float sigma:{1.f,2.999f}){cfg.supportSigma=sigma;auto output=c.dir/std::to_string(sigma);Require(!builder.Build({Gaussian(0)},3,cfg,output.string(),m,error)&&error.find("three")!=std::string::npos&&!fs::exists(output),"underbound support convention accepted or created output");}
        cfg.supportSigma=3;m=Build(c,{Gaussian(0)},cfg);m.supportSigma=1;Require(!FSplatSceneFormat::ValidateManifest(m,error),"external underbound manifest accepted");
    });
    suite.Test("R4 T08 replacing wanted work cancels obsolete queued reading and ready payloads",[]{
        Context c("wanted-cancel");auto cfg=Config();cfg.maxDepth=0;cfg.maxPageGaussians=1;auto m=Build(c,{Gaussian(0),Gaussian(1),Gaussian(2)},cfg);
        FSplatStreamingConfig config;config.cpuCacheBytes=65536;config.maxConcurrentReads=1;FSplatPageStreamer streamer;std::string error;Require(streamer.Open(m,(c.dir/"scene").string(),config,error),error);
        std::atomic<bool> entered{false};streamer.SetFaultInjector([&](unsigned page,unsigned){FSplatPageReadFault fault;if(page==0){entered=true;fault.delayMilliseconds=200;}return fault;});
        streamer.UpdateRequests({{0,100,1},{2,1,1}});auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(!entered&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));Require(entered,"delayed real read not scheduled");
        streamer.UpdateRequests({{1,200,1}});auto results=Wait(streamer);Require(results[0].pageId==1&&results[0].error.empty(),"obsolete read leaked through replacement wanted set");
        auto charged=streamer.GetMemoryStats().transferredBytes;streamer.UpdateRequests({});Require(charged>0&&streamer.GetMemoryStats().transferredBytes==charged,"wanted cancellation released consumer payload charge");Release(streamer,results[0]);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));Require(streamer.DrainReady().empty()&&streamer.GetStats().cancelled>=2,"obsolete queue/read result survived cancellation");
        streamer.Request({{2,1,1}});deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);while(streamer.GetMemoryStats().readyBytes==0&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));Require(streamer.GetMemoryStats().readyBytes>0,"ready cancellation fixture never completed");streamer.UpdateRequests({});Require(streamer.DrainReady().empty()&&streamer.GetMemoryStats().readyBytes==0,"obsolete ready payload retained");
        streamer.Shutdown();Require(streamer.GetMemoryStats().totalReservedBytes==0,"obsolete cancellation accounting did not close");
    });
}
