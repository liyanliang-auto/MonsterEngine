#pragma once
#include "SplatPresentationTests.h"
#include "Platform/Vulkan/VulkanCommandBuffer.h"

namespace SplatVerification {
/** Tiny raw offscreen graphics harness. It executes the actual compiled production
 * fragment/vertex SPIR-V, independently of desktop ImGui/WSI lifecycle tests. */
struct PresentationFragmentFixture {
    Device& d;VkDevice device;
    VkImage source=VK_NULL_HANDLE,target=VK_NULL_HANDLE;
    VkDeviceMemory sourceMemory=VK_NULL_HANDLE,targetMemory=VK_NULL_HANDLE;
    VkImageView sourceView=VK_NULL_HANDLE,targetView=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;VkDescriptorSetLayout setLayout=VK_NULL_HANDLE;
    VkDescriptorPool pool=VK_NULL_HANDLE;VkDescriptorSet set=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;VkRenderPass pass=VK_NULL_HANDLE;
    VkFramebuffer framebuffer=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;
    unsigned sourceEdge=0,outputEdge=0;bool uploaded=false;
    explicit PresentationFragmentFixture(Device& value):d(value),device(value.vk->getDevice()){}
    ~PresentationFragmentFixture(){
        d.rhi->waitForIdle();
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);
        if(pass)vkDestroyRenderPass(device,pass,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(pool)vkDestroyDescriptorPool(device,pool,nullptr);
        if(setLayout)vkDestroyDescriptorSetLayout(device,setLayout,nullptr);
        if(sampler)vkDestroySampler(device,sampler,nullptr);
        if(vertex)vkDestroyShaderModule(device,vertex,nullptr);
        if(fragment)vkDestroyShaderModule(device,fragment,nullptr);
        if(sourceView)vkDestroyImageView(device,sourceView,nullptr);
        if(targetView)vkDestroyImageView(device,targetView,nullptr);
        if(source)vkDestroyImage(device,source,nullptr);
        if(target)vkDestroyImage(device,target,nullptr);
        if(sourceMemory)vkFreeMemory(device,sourceMemory,nullptr);
        if(targetMemory)vkFreeMemory(device,targetMemory,nullptr);
    }
    void Image(unsigned edge,VkFormat format,VkImageUsageFlags usage,VkImage& image,VkDeviceMemory& memory,VkImageView& view){
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;
        ci.extent={edge,edge,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
        ci.usage=usage;ci.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        Require(vkCreateImage(device,&ci,nullptr,&image)==VK_SUCCESS,"fragment fixture image creation");
        VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,image,&req);
        VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(d.vk->getPhysicalDevice(),&properties);
        uint32_t type=UINT32_MAX;for(unsigned i=0;i<properties.memoryTypeCount;++i)
            if((req.memoryTypeBits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
        Require(type!=UINT32_MAX,"fragment fixture device memory type");
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=req.size;allocation.memoryTypeIndex=type;
        Require(vkAllocateMemory(device,&allocation,nullptr,&memory)==VK_SUCCESS,"fragment fixture image memory");
        Require(vkBindImageMemory(device,image,memory,0)==VK_SUCCESS,"fragment fixture bind memory");
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;
        vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        Require(vkCreateImageView(device,&vi,nullptr,&view)==VK_SUCCESS,"fragment fixture image view");
    }
    VkShaderModule Module(const fs::path& path){
        const auto bytes=Read(path);Require(bytes.size()%4==0,"SPIR-V byte alignment");
        std::vector<uint32_t> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
        VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};ci.codeSize=bytes.size();ci.pCode=words.data();
        VkShaderModule module=VK_NULL_HANDLE;Require(vkCreateShaderModule(device,&ci,nullptr,&module)==VK_SUCCESS,"production presentation shader module");return module;
    }
    void Initialize(unsigned sourceSize,unsigned outputSize,bool srgb){
        sourceEdge=sourceSize;outputEdge=outputSize;const VkFormat format=srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
        Image(sourceEdge,VK_FORMAT_R8G8B8A8_UNORM,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,source,sourceMemory,sourceView);
        Image(outputEdge,format,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT,target,targetMemory,targetView);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};si.magFilter=si.minFilter=VK_FILTER_LINEAR;
        si.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;si.addressModeU=si.addressModeV=si.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        Require(vkCreateSampler(device,&si,nullptr,&sampler)==VK_SUCCESS,"fragment fixture bilinear sampler");
        VkDescriptorSetLayoutBinding binding{};binding.binding=0;binding.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo dl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};dl.bindingCount=1;dl.pBindings=&binding;
        Require(vkCreateDescriptorSetLayout(device,&dl,nullptr,&setLayout)==VK_SUCCESS,"fragment fixture set layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=1;dp.pPoolSizes=&size;
        Require(vkCreateDescriptorPool(device,&dp,nullptr,&pool)==VK_SUCCESS,"fragment fixture descriptor pool");
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=pool;da.descriptorSetCount=1;da.pSetLayouts=&setLayout;
        Require(vkAllocateDescriptorSets(device,&da,&set)==VK_SUCCESS,"fragment fixture descriptor set");
        VkDescriptorImageInfo di{sampler,sourceView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&di;vkUpdateDescriptorSets(device,1,&write,0,nullptr);
        VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(FViewerPresentationPushConstants)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
        Require(vkCreatePipelineLayout(device,&pl,nullptr,&layout)==VK_SUCCESS,"fragment fixture pipeline layout");
        VkAttachmentDescription attachment{};attachment.format=format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
        VkSubpassDependency deps[2]{};deps[0].srcSubpass=VK_SUBPASS_EXTERNAL;deps[0].dstSubpass=0;deps[0].srcStageMask=VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;deps[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass=0;deps[1].dstSubpass=VK_SUBPASS_EXTERNAL;deps[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;deps[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;deps[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;deps[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&attachment;rp.subpassCount=1;rp.pSubpasses=&subpass;rp.dependencyCount=2;rp.pDependencies=deps;
        Require(vkCreateRenderPass(device,&rp,nullptr,&pass)==VK_SUCCESS,"fragment fixture render pass");
        VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=1;fb.pAttachments=&targetView;fb.width=fb.height=outputEdge;fb.layers=1;
        Require(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer)==VK_SUCCESS,"fragment fixture framebuffer");
        const char* root=std::getenv("MONSTER_SPLAT_SHADER_DIR");Require(root&&*root,"production presentation shader directory required");
        vertex=Module(fs::path(root)/"splat_present.vert.spv");fragment=Module(fs::path(root)/"splat_present.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};for(auto& stage:stages){stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stage.pName="main";}
        stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=vertex;stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=fragment;
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0,0,float(outputEdge),float(outputEdge),0,1};VkRect2D scissor{{0,0},{outputEdge,outputEdge}};
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;vp.pViewports=&viewport;vp.pScissors=&scissor;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.polygonMode=VK_POLYGON_MODE_FILL;rs.cullMode=VK_CULL_MODE_NONE;rs.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState blend{};blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};cb.attachmentCount=1;cb.pAttachments=&blend;
        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};gp.stageCount=2;gp.pStages=stages;gp.pVertexInputState=&vi;gp.pInputAssemblyState=&ia;gp.pViewportState=&vp;gp.pRasterizationState=&rs;gp.pMultisampleState=&ms;gp.pColorBlendState=&cb;gp.layout=layout;gp.renderPass=pass;
        Require(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&gp,nullptr,&pipeline)==VK_SUCCESS,"actual production presentation graphics pipeline");
    }
    std::vector<uint8_t> Render(const std::vector<uint8_t>& pixels,bool srgb,float strength){
        Require(pixels.size()==sourceEdge*sourceEdge*4,"fragment source fixture size");
        auto upload=Buffer(d.rhi,unsigned(pixels.size()),EMemoryUsage::Upload,pixels.data());
        auto readback=Buffer(d.rhi,outputEdge*outputEdge*4,EMemoryUsage::Readback);
        auto command=d.Begin();auto* list=dynamic_cast<RHI::Vulkan::FVulkanRHICommandListImmediate*>(command);
        Require(list&&list->GetVulkanCommandBuffer(),"fragment fixture command buffer");auto native=list->GetVulkanCommandBuffer()->getHandle();
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=uploaded?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.srcAccessMask=uploaded?VK_ACCESS_SHADER_READ_BIT:0;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=source;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={sourceEdge,sourceEdge,1};
        vkCmdCopyBufferToImage(native,static_cast<RHI::Vulkan::VulkanBuffer*>(upload.get())->getBuffer(),source,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=pass;begin.framebuffer=framebuffer;begin.renderArea.extent={outputEdge,outputEdge};
        vkCmdBeginRenderPass(native,&begin,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(native,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        vkCmdBindDescriptorSets(native,VK_PIPELINE_BIND_POINT_GRAPHICS,layout,0,1,&set,0,nullptr);
        const FViewerPresentationPushConstants pc{srgb?1u:0u,strength};vkCmdPushConstants(native,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(pc),&pc);vkCmdDraw(native,3,1,0,0);vkCmdEndRenderPass(native);
        copy.imageExtent={outputEdge,outputEdge,1};vkCmdCopyImageToBuffer(native,target,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,static_cast<RHI::Vulkan::VulkanBuffer*>(readback.get())->getBuffer(),1,&copy);
        VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=static_cast<RHI::Vulkan::VulkanBuffer*>(readback.get())->getBuffer();host.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(native,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
        d.Submit(command);uploaded=true;auto* mapped=static_cast<const uint8_t*>(readback->map());Require(mapped!=nullptr,"fragment result readback map");
        std::vector<uint8_t> result(mapped,mapped+outputEdge*outputEdge*4);readback->unmap();return result;
    }
};

inline void RunPresentationFragmentTests(Suite& suite,Device& d){
    for(bool srgb:{false,true}){
        suite.Test(std::string("PRESENT real fragment unity and bounded detail ")+(srgb?"SRGB":"UNORM"),[&,srgb]{
            PresentationFragmentFixture fixture(d);fixture.Initialize(8,8,srgb);
            const unsigned values[8]={20,20,20,64,200,235,235,235};std::vector<uint8_t> pixels(8*8*4);
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const auto i=(y*8+x)*4;pixels[i]=uint8_t(values[x]);pixels[i+1]=uint8_t(255-values[x]);pixels[i+2]=80;pixels[i+3]=uint8_t(64+y*20);}
            const auto neutral=fixture.Render(pixels,srgb,0),detail=fixture.Render(pixels,srgb,0.22f),capped=fixture.Render(pixels,srgb,20);
            unsigned changed=0;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)for(unsigned c=0;c<4;++c){const auto i=(y*8+x)*4+c;
                Require(std::abs(int(neutral[i])-int(pixels[i]))<=1,"unity path or attachment transfer changed encoded color");
                Require(capped[i]==detail[i],"shader gain escaped 0.22 cap");
                if(c==3)Require(detail[i]==neutral[i],"compensation altered alpha");
                else {const int lo=std::min({int(pixels[i]),int(pixels[(y*8+(x?x-1:0))*4+c]),int(pixels[(y*8+std::min(7u,x+1))*4+c])});
                    const int hi=std::max({int(pixels[i]),int(pixels[(y*8+(x?x-1:0))*4+c]),int(pixels[(y*8+std::min(7u,x+1))*4+c])});
                    Require(int(detail[i])>=lo-1&&int(detail[i])<=hi+1,"local contrast created out-of-range halo");
                    changed+=std::abs(int(detail[i])-int(neutral[i]))>1;
                }
            }
            Require(changed>8,"production shader did not compensate synthetic softened edge");
            Require(std::abs(int(detail[(3*8+3)*4])-59)<=2,"edge numerical response differs from expected bounded contrast");
        });
        for(unsigned output:{8u,16u})suite.Test(std::string("PRESENT real fragment constant preservation ")+(srgb?"SRGB":"UNORM")+" output="+std::to_string(output),[&,srgb,output]{
            PresentationFragmentFixture fixture(d);fixture.Initialize(8,output,srgb);std::vector<uint8_t> pixels(8*8*4);
            for(unsigned i=0;i<64;++i){pixels[i*4]=46;pixels[i*4+1]=10;pixels[i*4+2]=128;pixels[i*4+3]=173;}
            const auto result=fixture.Render(pixels,srgb,0.22f);
            for(unsigned i=0;i<output*output;++i)for(unsigned c=0;c<4;++c)
                Require(std::abs(int(result[i*4+c])-int(pixels[c]))<=1,"constant brightness/alpha not preserved by production shader");
        });
    }
}
} // namespace SplatVerification
