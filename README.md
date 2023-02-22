# Computer-Networks
NKU-COSC0010-计算机网络

以下实验均分99，如果需要保姆式手把手解析欢迎邮件联系我



### 一, 聊天室

1. #### 功能：
   
   + 双人聊天、多人群聊（多线程）
   
   + 群发或定点转发
   
   + 合理性检验
   
     

### 二.  UDP实现可靠性传输

1. ### 实验3-1

   + #### 基础功能

     + ##### 建立连接：

       实现类似于 TCP 的三次握手、四次挥手过程

     + ##### 差错检测：

       利用校验和进行差错检测，发送端将数据报看成 16 位整数序列，将整个数据报相加然后取反写入校验和域段，接收端将数据报用 0 补齐为 16 位整数倍，然后相加求和，如果计算结果为全 1，没有检测到错误；否则说明数据报存在差错。

     + ##### 流量控制（停等机制）：

       采用停等协议，发送端发送一个分组，然后等待接收端响应

     + ##### 日志输出：

       打印出三次握手四次挥手过程、序列号、确认序列号、数据大小、时延、吞吐率。

     + ##### 超时重传

       采用 rdt3.0 机制，由于通道既可能有差错，又可能有丢失，所以我们考虑利用rdt3.0 机制实现可靠数据传输。

   + #### 附加功能

     + ##### MSS协商

       双方将会协商MSS，选择双⽅需求的最小MSS作为通信MSS。

     + ##### 多线程

       为了兼容后期拥塞控制的实验，本次代码在设计上采⽤多线程控制，由发送线程和接收线程互相配合完成发送或者接收的任务。

     + ##### 异常检测

       1. 断开方式与TCP基本相同，为了保证通信状态正常，在没有任何信息需要发送时，双⽅也会在固定的时间内发送⼀个小数据包，以检测连接状态和报告⾃身情况。当数据包出现10次连续丢失时，双方将认为通信异常，自动启动断开程序。
       
          

2. ### 实验3-2 + 3-3[补充于3-1]

   + **拥塞控制**：在实验 3-2 的基础上，利用 RENO 算法实现拥塞控制，实现了超时检测和三次重复 ACK 检测。

   + **流量控制**：在实验 3-1 的基础上，将停等机制改成基于滑动窗口的流量控制机制, 采用 GBN方法。

   + **快速重传**：接收到 3 个重复 ACK 时快速重传收到 ACK 序列号所指示的报文段。

     

3. ### 实验3-4

   + 停等机制与滑动窗口机制性能对比
   + 滑动窗口机制中不同窗口大小对性能的影响
   + 有拥塞控制和无拥塞控制的性能比较
