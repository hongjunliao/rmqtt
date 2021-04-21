tests for rmqtt

pub_file:
测试mqt:将一个100m文件通过mqttbroker发给1000个客户端 
并校验md5值,需要指出的是 文件发送方并不需要等所有接收方上线才开始发送

接收方, IDs.txt为客户端ID文件
pub_file --mqtt_addr=tcp://192.168.1.103:7006 -t file --ids=IDs.txt  --dir=recv/
发布方
pub_file --mqtt_addr=tcp://192.168.1.103:7006 -t file --file=10M