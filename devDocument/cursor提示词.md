





-----------

## 开发提示词

1.帮我参考https://learnopengl-cn.github.io/01%20Getting%20started/08%20Coordinate%20Systems/中的opengl教程，使用monster引擎、使用vulkan实现一个旋转的立方体，并且立方体使用贴图，使用的纹理路径：E:\MonsterEngine\resources\textures\container.jpg,  E:\MonsterEngine\resources\textures\awesomeface.png;

以下是learnOpenGL旋转的立方体demo的代码：

```
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <learnopengl/shader_m.h>

#include <iostream>

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);

// settings
const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

int main()
{
    // glfw: initialize and configure
    // ------------------------------
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    // glfw window creation
    // --------------------
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    // configure global opengl state
    // -----------------------------
    glEnable(GL_DEPTH_TEST);

    // build and compile our shader zprogram
    // ------------------------------------
    Shader ourShader("6.2.coordinate_systems.vs", "6.2.coordinate_systems.fs");

    // set up vertex data (and buffer(s)) and configure vertex attributes
    // ------------------------------------------------------------------
    float vertices[] = {
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,  0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f
    };
    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // texture coord attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);


    // load and create a texture 
    // -------------------------
    unsigned int texture1, texture2;
    // texture 1
    // ---------
    glGenTextures(1, &texture1);
    glBindTexture(GL_TEXTURE_2D, texture1);
    // set the texture wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // load image, create texture and generate mipmaps
    int width, height, nrChannels;
    stbi_set_flip_vertically_on_load(true); // tell stb_image.h to flip loaded texture's on the y-axis.
    unsigned char *data = stbi_load(FileSystem::getPath("resources/textures/container.jpg").c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Failed to load texture" << std::endl;
    }
    stbi_image_free(data);
    // texture 2
    // ---------
    glGenTextures(1, &texture2);
    glBindTexture(GL_TEXTURE_2D, texture2);
    // set the texture wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // load image, create texture and generate mipmaps
    data = stbi_load(FileSystem::getPath("resources/textures/awesomeface.png").c_str(), &width, &height, &nrChannels, 0);
    if (data)
    {
        // note that the awesomeface.png has transparency and thus an alpha channel, so make sure to tell OpenGL the data type is of GL_RGBA
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    else
    {
        std::cout << "Failed to load texture" << std::endl;
    }
    stbi_image_free(data);

    // tell opengl for each sampler to which texture unit it belongs to (only has to be done once)
    // -------------------------------------------------------------------------------------------
    ourShader.use();
    ourShader.setInt("texture1", 0);
    ourShader.setInt("texture2", 1);


    // render loop
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        // input
        // -----
        processInput(window);

        // render
        // ------
        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // also clear the depth buffer now!

        // bind textures on corresponding texture units
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture2);

        // activate shader
        ourShader.use();

        // create transformations
        glm::mat4 model         = glm::mat4(1.0f); // make sure to initialize matrix to identity matrix first
        glm::mat4 view          = glm::mat4(1.0f);
        glm::mat4 projection    = glm::mat4(1.0f);
        model = glm::rotate(model, (float)glfwGetTime(), glm::vec3(0.5f, 1.0f, 0.0f));
        view  = glm::translate(view, glm::vec3(0.0f, 0.0f, -3.0f));
        projection = glm::perspective(glm::radians(45.0f), (float)SCR_WIDTH / (float)SCR_HEIGHT, 0.1f, 100.0f);
        // retrieve the matrix uniform locations
        unsigned int modelLoc = glGetUniformLocation(ourShader.ID, "model");
        unsigned int viewLoc  = glGetUniformLocation(ourShader.ID, "view");
        // pass them to the shaders (3 different ways)
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, &view[0][0]);
        // note: currently we set the projection matrix each frame, but since the projection matrix rarely changes it's often best practice to set it outside the main loop only once.
        ourShader.setMat4("projection", projection);

        // render box
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);


        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        // -------------------------------------------------------------------------------
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // optional: de-allocate all resources once they've outlived their purpose:
    // ------------------------------------------------------------------------
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    // ------------------------------------------------------------------
    glfwTerminate();
    return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
// ---------------------------------------------------------------------------------------------------------
void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and 
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);
}

```



vs:

```
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
	gl_Position = projection * view * model * vec4(aPos, 1.0f);
	TexCoord = vec2(aTexCoord.x, aTexCoord.y);
}
```

fs:

```
#version 330 core
out vec4 FragColor;

in vec2 TexCoord;

// texture samplers
uniform sampler2D texture1;
uniform sampler2D texture2;

void main()
{
	// linearly interpolate between both textures (80% container, 20% awesomeface)
	FragColor = mix(texture(texture1, TexCoord), texture(texture2, TexCoord), 0.2);
}
```







其他：

1.系统内存分配使用：FMemoryManage，GPU内存分配使用：FVulkanMemoryManager

1.文件命名和函数命名均参考UE5；

1.添加详细的代码注释和代码说明，尽量使用中文注释；注释中的专业词汇和关键词使用英文；MR_LOG_DEBUG中的字符串使用英文；并将该文件保存为 Unicode 格式；

1.新增文件添加到VS2022项目，并保证工程编译通过，工程文件为E:\MonsterEngine\MonsterEngine.sln；

2.所有代码实现参考UE5，**UE5风格架构** - 严格参考UE5设计，UE5源码github链接：@https://github.com/EpicGames/UnrealEngine ；

3.当前引擎的现有实现见开发文档：开发文档路径：E:\MonsterEngine\devDocument\引擎的架构和设计.md

3.保证在visual studio2022中可以编译和链接通过；

4.引擎的开发文档见：E:\MonsterEngine\devDocument\引擎的架构和设计.md：，我的终极目标是实现一个像UE5一样的现代渲染引擎。

5.文件结构参照UE5;

6.使用英文注释和英文日志log；

7.提出下一步开发计划；

8.不要生成文档，不要生成markdown文档；



8.分析过程使用中文；

7.生成一个对应的开发文档，中文，markdown格式，放到文件夹下： E:\MonsterEngine\devDocument\

-   （1）把本次的代码实现整理成文档内容；
-   （2）画出类UML图、线程图、代码架构图、代码流程图，并做好排版，补充到文档E:\MonsterEngine\devDocument\引擎的架构和设计.md的最后
-   ​    3）并提出下一步开发计划，补充到文档E:\MonsterEngine\devDocument\引擎的架构和设计.md的最后







所有代码实现参考UE5，**UE5风格架构** - 严格参考UE5设计，UE5源码github链接：@https://github.com/EpicGames/UnrealEngine ；

修改完成后要确保visual studio2022编译通过，工程路径：E:\MonsterEngine\MonsterEngine.sln

visual studio 2022安装路径：E:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE



我的终极目标是实现一个像UE5一样的现代渲染引擎，实现多线程渲染、前向管线、延迟管线、实时阴影等，实现一个完整的渲染引擎，帮我规划未来的开发计划和排期，帮我生成一个文档，中文，markdown格式，放到文件夹下： E:\MonsterEngine\devDocument\

------





帮我完善日志系统Log.h，可以参考UE5的`UE_LOG` 宏UE5中的，

| **日志宏与接口** | `LogMacros.h` `LogVerbosity.h` | `UE_LOG` `DECLARE_LOG_CATEGORY_EXTERN`等 | 提供日志记录接口、定义日志级别和声明日志类别。 |
| ---------------- | ------------------------------ | ---------------------------------------- | ---------------------------------------------- |
|                  |                                |                                          |                                                |

用法包括：`UE_LOG` 是UE5中最基础的日志记录宏，其标准格式如下：

cpp

```
UE_LOG(日志类别, 详细级别, TEXT("格式化消息"), 参数...);
```



**参数解析**：

- **日志类别**：对日志进行分类。你可以使用内置的 `LogTemp`，或为不同模块创建自定义类别。

- **详细级别**：控制日志的重要性，从高到低主要有 `Fatal`（导致崩溃）、`Error`（红色错误）、`Warning`（黄色警告）、`Display`/`Log`（普通信息）、`Verbose`/`VeryVerbose`（详细调试信息，通常需手动开启）。

- **格式化消息**：支持多种数据类型：

  cpp

  ```
  // FString
  UE_LOG(LogTemp, Warning, TEXT("Actor名称: %s"), *Actor->GetName());
  // 布尔值
  UE_LOG(LogTemp, Warning, TEXT("布尔值: %s"), bIsTrue ? TEXT("真") : TEXT("假"));
  // 整型、浮点型
  UE_LOG(LogTemp, Warning, TEXT("整数: %d, 浮点数: %f"), MyInt, MyFloat);
  // FVector
  UE_LOG(LogTemp, Warning, TEXT("位置: %s"), *MyVector.ToString());
  ```

使用**中文注释**，关键词或英语特有名词使用英文注释；；MR_LOG_DEBUG中的字符串使用英文；

修改关联的代码;

不要生成markdown文档；



日志系统支持中文；



## 生成文档提示词

------

帮我针对三角形的渲染流程代码整理成一个文档（中文），画图说明，画出代码结构图、UML图和代码流程图。

相关的类包括：class TriangleRenderer、class TriangleApplication



详细分析RHI，详细分析vulkan的代码架构，

涉及的代码文件夹：E:\MonsterEngine\Source\Platform\Vulkan和E:\MonsterEngine\Include\Platform\Vulkan



.生成的文档，使用中文，markdown格式，放到文件夹下： E:\MonsterEngine\devDocument\

**中文注释**



------



## 面条文档提示词

帮我针对rect_collision_detector.cpp、rect_collision_detector.h  的class RectCollisionDetector  和 rect_collision_node.h 中的class QuadTreeNode代码整理成一个面试文档（中文），整理开场白模板，针对面试官可能会问到的问题整理成回答的话术，尽量画图说明，尽量画出代码结构图、UML图和代码流程图。

相关的类包括：class RectCollisionDetector极其相关联的class;

.生成的文档，使用中文，markdown格式，放到文件夹下： D:\00.Code\hd_021\map-render\myDocuments





## to do list:

1.注释翻译成中文；

MR_LOG_DEBUG需要支持中文字符串；

删除旧的内存系统；

跑testdemo

启动渲染运行崩溃解决；

继承tracy;

先保证面条；





## 下一步：

阅读代码：Source/Platform/Vulkan/VulkanCommandBuffer.cpp