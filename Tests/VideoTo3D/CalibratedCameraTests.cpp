#include "Core/MonsterEnginePCH.h"
#include "Renderer/Splat/SplatViewerSession.h"
#include "Renderer/Splat/SplatPLYStream.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <fstream>
#include <iomanip>
#include <stdexcept>
using namespace MonsterRender::Splat;
static int checks=0;
static void Check(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}
static FCalibratedCameraDesc Camera(){
    FCalibratedCameraDesc d{};d.width=641;d.height=479;d.fx=431.75;d.fy=517.25;
    d.cx=287.125;d.cy=251.875;d.nearPlane=.01f;d.farPlane=1e10f;d.shDegree=3;
    // Rotation about Z plus translation: camera eye (-2,1,-3).
    d.viewMatrix[1]=1;d.viewMatrix[4]=-1;d.viewMatrix[10]=1;d.viewMatrix[15]=1;
    d.viewMatrix[12]=1;d.viewMatrix[13]=2;d.viewMatrix[14]=3;return d;
}
static void Reject(const FCalibratedCameraDesc& d){
    FCameraUniforms out;std::memset(&out,0x31,sizeof(out));auto before=out;std::string error;
    Check(!FSplatViewerSession::BuildCalibratedCamera(d,out,error),"invalid camera accepted");
    Check(!error.empty(),"rejection did not report reason");
    Check(std::memcmp(&out,&before,sizeof(out))==0,"rejection modified output");
}
static void CheckPLY(const std::string& directory){
    for(unsigned degree=0;degree<=3;++degree){
        auto path=directory+"/activated-sh"+std::to_string(degree)+".ply";
        std::ofstream ply(path);ply<<std::setprecision(17);
        ply<<"ply\nformat ascii 1.0\nelement vertex 1\n";
        for(auto name:{"x","y","z","f_dc_0","f_dc_1","f_dc_2"})ply<<"property float "<<name<<"\n";
        unsigned perChannel=(degree+1)*(degree+1)-1;
        for(unsigned i=0;i<3*perChannel;++i)ply<<"property float f_rest_"<<i<<"\n";
        for(auto name:{"opacity","scale_0","scale_1","scale_2","rot_0","rot_1","rot_2","rot_3"})ply<<"property float "<<name<<"\n";
        ply<<"end_header\n1 -2 3 .11 -.07 .19 ";
        for(unsigned channel=0;channel<3;++channel)for(unsigned k=1;k<=perChannel;++k)ply<<(100*channel+k)*.001<<" ";
        ply<<".73 "<<std::log(.24)<<" "<<std::log(.105)<<" "<<std::log(.16)<<" 2 0 0 2\n";ply.close();
        FSplatPLYHeader header;std::string error;
        Check(FSplatPLYStream::Probe(path,header,error),"PLY probe rejected valid SH fixture");
        Check(header.shDegree==degree,"PLY SH degree detection");
        FSplatBuildControl control;std::vector<FSplatGaussian> records;
        Check(FSplatPLYStream::StreamRecords(path,header,1,control,[&](std::span<const FSplatGaussian> rows,const FSplatBuildSourceCursor&,std::string&){records.assign(rows.begin(),rows.end());return true;},error),"PLY activation/reordering stream");
        Check(records.size()==1,"PLY fixture row count");auto& row=records[0];
        Check(std::abs(row.scale[0]-.24)<1e-5&&std::abs(row.scale[1]-.105)<1e-5&&std::abs(row.scale[2]-.16)<1e-5,"PLY log-scale activation");
        Check(std::abs(row.opacity-1/(1+std::exp(-.73)))<1e-5,"PLY sigmoid activation");
        Check(std::abs(row.rotation[0]-std::sqrt(.5))<1e-5&&std::abs(row.rotation[3]-std::sqrt(.5))<1e-5,"PLY WXYZ quaternion normalization");
        Check(std::abs(row.sh[0]-.11)<1e-5&&std::abs(row.sh[1]+.07)<1e-5&&std::abs(row.sh[2]-.19)<1e-5,"PLY asymmetric DC channels");
        for(unsigned channel=0;channel<3;++channel)for(unsigned k=1;k<=perChannel;++k)
            Check(std::abs(row.sh[3*k+channel]-(100*channel+k)*.001)<1e-5,"PLY channel-major to coefficient-major SH reorder");
    }
}
int main(int argc,char** argv){try{
    static_assert(sizeof(FCameraUniforms)==176);
    auto d=Camera();FCameraUniforms out{};std::string error;
    Check(FSplatViewerSession::BuildCalibratedCamera(d,out,error),"valid offcenter camera rejected");
    Check(std::abs(out.camPos[0]+2)<1e-5&&std::abs(out.camPos[1]-1)<1e-5&&std::abs(out.camPos[2]+3)<1e-5,"rigid inverse eye incorrect");
    for(double depth:{.1,1.,4.,19.})for(double x:{-.15,.0,.21})for(double y:{-.11,.0,.13}){
        // Independently compute native bottom-up NDC then top-left pixel centers.
        double nx=(out.projMatrix[0]*x-out.projMatrix[8]*depth)/depth;
        double ny=(out.projMatrix[5]*y-out.projMatrix[9]*depth)/depth;
        double u=((nx+1)*d.width-1)/2;
        double v=d.height-1-((ny+1)*d.height-1)/2;
        Check(std::abs(u-(d.fx*x/depth+d.cx))<=1e-3,"offcenter unequal-focal horizontal projection");
        Check(std::abs(v-(d.cy-d.fy*y/depth))<=1e-3,"offcenter unequal-focal vertical projection");
    }
    for(int which=0;which<22;++which){auto bad=d;
        switch(which){
        case 0:bad.width=0;break;case 1:bad.height=16385;break;case 2:bad.fx=0;break;
        case 3:bad.fy=-1;break;case 4:bad.cx=std::numeric_limits<double>::infinity();break;
        case 5:bad.cy=std::numeric_limits<double>::quiet_NaN();break;case 6:bad.nearPlane=0;break;
        case 7:bad.farPlane=bad.nearPlane;break;case 8:bad.shDegree=-1;break;case 9:bad.shDegree=4;break;
        case 10:bad.viewMatrix[12]=std::numeric_limits<float>::infinity();break;
        case 11:bad.viewMatrix[3]=.1f;break;case 12:bad.viewMatrix[15]=0;break;
        case 13:bad.viewMatrix[1]=-1;break;case 14:bad.viewMatrix[1]=0;break;
        case 15:bad.viewMatrix[1]=2;break;case 16:bad.viewMatrix[0]=.2f;break;
        case 17:bad.fx=1e100;break;case 18:bad.fx=1e-100;break;
        case 19:bad.nearPlane=std::numeric_limits<float>::quiet_NaN();break;
        case 20:bad.farPlane=std::numeric_limits<float>::infinity();break;
        case 21:bad.cx=1e15;break;
        }Reject(bad);
    }
    float eye[3]={1,2,3},right[3]={1,0,0},up[3]={0,1,0},forward[3]={0,0,-1};
    auto old=FSplatViewerSession::BuildCamera(eye,right,up,forward,45,.1f,100.f,640,480,3);
    Check(old.projMatrix[8]==0&&old.projMatrix[9]==0,"legacy centered projection changed");
    Check(std::abs(old.focalY-480/(2*std::tan(45*3.141592653589793/360)))<1e-3,"legacy FOV changed");
    Check(argc==2,"output directory argument required");CheckPLY(argv[1]);
    std::cout<<"PASS calibrated camera checks="<<checks<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<" checks="<<checks<<"\n";return 1;}}
