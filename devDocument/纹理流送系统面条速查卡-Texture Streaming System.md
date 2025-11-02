# 纹理流送系统面试速查卡 ⚡

> 5 分钟快速复习，面试前必看！

---

## 🎯 核心架构（30秒记住）

```
FTextureStreamingManager (管理层)
    ↓ owns
FTexturePool (内存池 512MB)
    ↓ uses
FAsyncFileIO (异步IO)
```

**三句话总结**：
1. **Manager 管调度**：计算优先级、决定加载哪些 Mip
2. **Pool 管内存**：预分配池、Free-List 快速分配
3. **AsyncIO 管读取**：多线程、不阻塞主线程

---

## 🔢 必背数字

| 指标 | 数值 | 含义 |
|------|------|------|
| **内存节省** | 86% | 50GB → 7GB（50个16K纹理） |
| **Pool 大小** | 512MB | 预分配内存池 |
| **对齐大小** | 256B | GPU 友好对齐 |
| **Worker 线程** | 2 个 | 异步 IO 线程数 |
| **分配延迟** | 50ns | vs malloc 1000ns（快20x） |
| **碎片率** | 2-5% | vs 传统 15-30% |
| **Mip 数量** | 最多 16 | 一个纹理最多 16 个 Mip |
| **LRU 阈值** | 0.5 | 优先级 < 0.5 可被驱逐 |

---

## 💬 10 个必会问题速答

### Q1: 什么是纹理流送？

**1 句话**：按需加载纹理 Mip 级别，节省内存。

**关键词**：Mip、按需、内存节省 86%、超大纹理。

**数据**：16K 纹理 1GB → 只加载需要的 Mip = 50-100MB。

---

### Q2: 三大核心类是什么？

**记忆口诀**：管-内-IO（Manager-Pool-AsyncIO）

```
FTextureStreamingManager:  调度、优先级、Mip 加载/卸载
FTexturePool:             预分配池、Free-List、碎片管理
FAsyncFileIO:             多线程、队列、异步读取
```

---

### Q3: 优先级怎么算？

**公式**：

```cpp
Priority = (1.0 / Distance) × ScreenSize × TimeFactor
```

**因素**：
- **距离**：近 = 高，远 = 低
- **屏幕大小**：占比大 = 高
- **时间**：最近使用 = 高

**映射**：
- Priority > 0.8 → 全部 Mip
- Priority > 0.5 → 缺 2 个最低 Mip
- Priority > 0.2 → 一半 Mip
- Priority < 0.2 → 只最低 Mip

---

### Q4: LRU 是什么？

**1 句话**：内存不足时，驱逐最久未使用的纹理 Mip。

**流程**：
1. 找优先级 < 0.5 的纹理
2. 按优先级从低到高排序
3. 逐个驱逐一半 Mip
4. 直到释放足够空间

**关键词**：Least Recently Used、优先级、驱逐、空间回收。

---

### Q5: Free-List 算法是什么？

**1 句话**：链表管理空闲区域，First-Fit 分配。

**结构**：

```
Pool: [Used][Free1][Used][Free2][Used]
        ↓      ↓      ↓
     FreeList → Free1 → Free2 → nullptr
```

**分配**：遍历链表，找第一个足够大的区域。

**释放**：加回链表，自动合并相邻空闲区。

**时间**：O(N)，N = 空闲区域数。

---

### Q6: 异步 IO 怎么工作？

**模式**：生产者-消费者

```
主线程（生产者）          Worker 线程（消费者）
    ↓                          ↓
 ReadAsync → Queue → notify → Worker 1
                            → Worker 2
                              ↓
                          ProcessRequest
                              ↓
                          OnComplete 回调
```

**优势**：
- 主线程不阻塞
- 多线程并行读取
- 吞吐量 2x+

---

### Q7: 如何避免卡顿？

**4 个方法**：

1. **异步 IO**：不阻塞主线程
2. **分帧加载**：每帧最多 2ms
3. **预测预加载**：根据移动方向提前加载
4. **优先级调度**：高优先级立即，低优先级延后

**效果**：帧时间从 16ms → 100ms（卡顿）优化到稳定 16ms。

---

### Q8: 如何处理碎片？

**3 层防御**：

1. **自动合并**：释放时合并相邻空闲区
2. **对齐分配**：256B 对齐，减少内部碎片
3. **定期紧缩**：Compact() 手动触发

**效果**：碎片率从 15-30% 降到 2-5%。

---

### Q9: 与虚拟纹理有何区别？

| 特性 | Texture Streaming | Virtual Texture |
|------|------------------|----------------|
| **粒度** | Mip 级别 | Tile (128x128) |
| **管理** | CPU | GPU 直接寻址 |
| **复杂度** | 中 | 高 |
| **节省** | 86% | 95%+ |
| **适用** | 通用纹理 | 超大地形 |

**1 句话**：Streaming 按 Mip 管理，VT 按 Tile 管理，更细粒度。

---

### Q10: 与 UE5 对比如何？

**一致性**：**82%** 🎯

**相同**：
- ✅ Mip 级别流送
- ✅ LRU 驱逐
- ✅ 异步加载
- ✅ Free-List 内存池

**差异**：
- ⚠️ 优先级算法简化（UE5 更复杂）
- ❌ 无预测预加载
- ❌ 无虚拟纹理

**总结**：核心功能完整，高级特性待扩展。

---

## 📊 性能对比速记

| 指标 | 传统方案 | 流送系统 | 提升 |
|------|---------|---------|------|
| **内存** | 50GB | 7GB | **-86%** 💾 |
| **分配延迟** | 1000ns | 50ns | **20x** ⚡ |
| **碎片率** | 20% | 3% | **-85%** |
| **IO 吞吐** | 200MB/s | 400MB/s | **2x** |
| **帧卡顿** | 有 | 无 | **流畅** ✅ |

---

## 🎨 核心流程图（闭眼能画）

### 流程 1：完整流送

```
注册纹理 → 每帧更新 → 计算优先级 → 排序
                                    ↓
                          需要 StreamIn?
                                 ↓ 是
                          Pool 有空间?
                              ↓ 否
                          LRU 驱逐
                              ↓
                          Pool.Allocate
                              ↓
                       AsyncIO.ReadAsync
                              ↓
                       Worker Thread 读取
                              ↓
                        OnComplete 回调
                              ↓
                       更新 ResidentMips
```

### 流程 2：LRU 驱逐

```
需要 50MB → Pool 不足 → 找低优先级纹理
                            ↓
                    按优先级排序（低→高）
                            ↓
                    逐个驱逐一半 Mip
                            ↓
                      释放空间足够?
                        ↓ 是
                      停止驱逐
                        ↓
                    尝试再次分配
```

---

## 🔑 代码速记（3 个必写函数）

### 1. 优先级计算

```cpp
void UpdatePriorities() {
    for (auto& st : StreamingTextures) {
        float priority = (1.0f / Distance) * ScreenSize;
        
        if (priority > 0.8f)
            st.RequestedMips = TotalMipLevels;      // 全部
        else if (priority > 0.5f)
            st.RequestedMips = TotalMipLevels - 2;  // 缺2个
        else
            st.RequestedMips = 1;                   // 最低
    }
}
```

### 2. Free-List 分配

```cpp
void* AllocateFromFreeList(SIZE_T Size, SIZE_T Align) {
    FFreeRegion* region = FreeList;
    
    while (region) {
        SIZE_T alignedOffset = (region->Offset + Align - 1) & ~(Align - 1);
        SIZE_T padding = alignedOffset - region->Offset;
        
        if (region->Size >= Size + padding) {
            void* ptr = PoolMemory + alignedOffset;
            // 更新/移除 region
            return ptr;
        }
        region = region->Next;
    }
    return nullptr;
}
```

### 3. 异步读取

```cpp
void WorkerThreadFunc() {
    while (!bShuttingDown) {
        FInternalRequest* req = nullptr;
        
        // 等待请求
        {
            std::unique_lock lock(QueueMutex);
            QueueCV.wait(lock, [this]() {
                return !RequestQueue.empty() || bShuttingDown;
            });
            
            if (!RequestQueue.empty()) {
                req = RequestQueue.front();
                RequestQueue.pop();
            }
        }
        
        // 处理
        if (req) {
            bool success = ProcessRequest(*req);
            req->Promise.set_value(success);
            if (req->Request.OnComplete) {
                req->Request.OnComplete(success, bytesRead);
            }
        }
    }
}
```

---

## 💡 万能回答模板

**面试官问任何问题，按此模板回答**：

1. **定义**（10秒）：这是什么？
   > "纹理流送是按需加载 Mip 级别的技术..."

2. **原理**（20秒）：怎么工作？（画图）
   > "系统分三层：Manager、Pool、AsyncIO..."

3. **优势**（15秒）：为什么这样设计？
   > "节省内存 86%、避免卡顿、支持超大纹理..."

4. **数据**（10秒）：性能提升多少？
   > "分配延迟从 1000ns 降到 50ns，快 20 倍..."

5. **对比**（5秒）：与 XX 方案对比
   > "相比传统方案，碎片率降低 6 倍..."

**总时长**：60 秒，简洁有力！

---

## 🎯 白板题准备

### 题目 1：实现 Free-List 分配器

**要求**：
- First-Fit 算法
- 支持对齐
- 自动合并

**关键点**：
1. 遍历 FreeList
2. 计算对齐偏移
3. 检查大小是否足够
4. 更新/移除区域

### 题目 2：实现 LRU 驱逐

**要求**：
- 找低优先级纹理
- 按优先级排序
- 逐个驱逐

**关键点**：
1. 过滤 priority < 0.5
2. std::sort 排序
3. 循环驱逐直到满足

### 题目 3：设计异步 IO 系统

**要求**：
- 生产者-消费者模式
- 线程安全
- 回调支持

**关键点**：
1. 队列 + 互斥锁
2. 条件变量同步
3. Worker 线程循环

---

## 📝 面试检查清单

**开场 5 分钟**：
- [ ] 介绍系统架构（3 层）
- [ ] 说明核心优势（内存节省 86%）
- [ ] 给出关键数据

**深入 10 分钟**：
- [ ] 解释优先级计算
- [ ] 画 Free-List 结构图
- [ ] 描述异步 IO 流程

**高级 5 分钟**：
- [ ] LRU 驱逐策略
- [ ] 碎片处理
- [ ] 与 UE5 对比

**白板 10 分钟**：
- [ ] 实现 AllocateFromFreeList
- [ ] 实现 EvictLowPriorityTextures

---

## 🎤 开场白速记版（30秒）

> "我实现的纹理流送系统参考 UE5，**核心是按需加载 Mip 级别**。
>
> **三层架构**：Manager 管调度、Pool 管内存、AsyncIO 管读取。
>
> **关键数据**：节省 86% 内存、分配快 20 倍、碎片率降 85%。
>
> 系统使用 **LRU 驱逐**低优先级纹理，**Free-List** 快速分配，**异步 IO** 避免卡顿。
>
> 可以详细展开任何点。"

---

## ✅ 面试前检查

**前 1 天**：
- [ ] 通读主文档 1 遍
- [ ] 默写核心架构图
- [ ] 复习关键数据

**前 1 小时**：
- [ ] 看速查卡 1 遍
- [ ] 默写 3 个核心函数
- [ ] 练习开场白

**面试中**：
- [ ] 自信、清晰、逻辑
- [ ] 画图说明
- [ ] 给出数据
- [ ] 主动对比

---

**快速复习版本**：v1.0  
**最后更新**：2025-11-02  
**建议复习时间**：面试前 1 小时

