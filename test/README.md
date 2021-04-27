tests for rmqtt

pub_file:
测试mqt:将一个100m文件通过mqttbroker发给1000个客户端 
并校验md5值,需要指出的是 文件发送方并不需要等所有接收方上线才开始发送

发布示例, 发布方将文件10M发布到主题"10M"
pub_file --mqtt_addr=tcp://192.168.1.103:7006 -t 10M --file=10M

接收示例, 接收方从主题"10M"接收消息, 并保存为recv/10M/${ID}文件, IDs.txt为客户端ID文件
pub_file --mqtt_addr=tcp://192.168.1.103:7006 -t 10M --ids=file://IDs.txt  --dir=recv/

检查程序示例, 检查程序检查recv/${ID}文件, 通过对比与文件10M的MD5码,打印成功率
