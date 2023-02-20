package main

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/castai/promwrite"
	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/julienschmidt/httprouter"
	"gopkg.in/yaml.v2"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"sync"
	"syscall"
	"time"
)

var (
	promclient *promwrite.Client
	ctx        context.Context
	cancel     context.CancelFunc
	mqclient   mqtt.Client
)

type conf struct {
	Backend       string `yaml:"backend"`
	Backendtoken  string `yaml:"backendtoken"`
	Mqtt_broker   string `yaml:"mqtt_broker"`
	Mqtt_username string `yaml:"mqtt_username"`
	Mqtt_passwd   string `yaml:"mqtt_passwd"`
	Mqtt_topic    string `yaml:"mqtt_topic"`
	Http_port     string `yaml:"http_port"`
}

var config *conf

func send2prometheus(data []byte) error {
	var bodymap map[string]interface{}
	if err := json.Unmarshal([]byte(data), &bodymap); err != nil {
		fmt.Printf("send2prometheus:err:%v", err)
	}

	labelpart := make(map[string]interface{})
	datapart := make(map[string]interface{})

	var label []promwrite.Label
	var sample []promwrite.TimeSeries
	for type_k, type_v := range bodymap {
		fmt.Printf("send2prometheus,bodymap,type_k:%v,type_v:%v\n", type_k, type_v)
		// check this is label part
		if strings.Contains(type_k, "lab_") {
			labelpart[type_k] = type_v
			label = append(label, promwrite.Label{type_k, type_v.(string)})
		} else {
			// this data metric part
			datapart[type_k] = float64(type_v.(float64))
		}
	}
	now := time.Now().UTC()
	// transfer all data to  TimeSeries
	for key, value := range datapart {
		sample = append(sample, promwrite.TimeSeries{
			append(label, promwrite.Label{"__name__", key}),
			promwrite.Sample{
				now,
				float64(value.(float64))},
		})
	}

	//fmt.Printf("send2prometheus,data:%v,hostname:%v,wendu:%v,shidu:%v,smoke:%v\n", string(data),tmp.Hostname,tmp.Wendu, tmp.Shidu,tmp.Smoke)
	req := &promwrite.WriteRequest{sample}
	/*req := &promwrite.WriteRequest{
		TimeSeries: []promwrite.TimeSeries{
			{
				Labels: []promwrite.Label{
					{
						Name:  "__name__",
						Value: "wendu",
					},
				},
				Sample: promwrite.Sample{
					Time:  now,
					Value: float64(tmp.Wendu),
				},
			},
			{
				Labels: []promwrite.Label{
					{
						Name:  "__name__",
						Value: "shidu",
					},
				},
				Sample: promwrite.Sample{
					Time:  now,
					Value: float64(tmp.Shidu),
				},
			},
		},
	}*/

	fmt.Printf("start to write,req:%v\n", req)
	_, err := promclient.Write(ctx, req, promwrite.WriteHeaders(map[string]string{"Authorization": config.Backendtoken}))
	if err != nil {
		fmt.Printf("send to prometheus err:%v", err)
		return err
	}
	return nil
}

/**
 * @Description:订阅回调
 * @param client
 * @param msg
 */

func subCallBackFunc(client mqtt.Client, msg mqtt.Message) {
	fmt.Printf("订阅: 当前话题是 [%s]; 信息是 [%s] \n", msg.Topic(), string(msg.Payload()))
	// send this mqtt msg to prometheus
	send2prometheus(msg.Payload())

}

/**
 * @Description:订阅消息
 */

func subscribe() {
	mqclient.Subscribe(config.Mqtt_topic, 0x00, subCallBackFunc)
}

func initClient() {
	promclient = promwrite.NewClient(
		config.Backend,
		promwrite.HttpClient(&http.Client{
			Timeout: 30 * time.Second,
		}),
	)
	fmt.Printf("###initClient--0")
	// init mqtt client
	opts := mqtt.NewClientOptions()

	// update for your emqx account
	opts.AddBroker(config.Mqtt_broker)
	opts.SetUsername(config.Mqtt_username)
	opts.SetPassword(config.Mqtt_passwd)

	mqclient = mqtt.NewClient(opts)
	if token := mqclient.Connect(); token.Wait() && token.Error() != nil {
		log.Panic("订阅 MQTT 失败")
	}
	fmt.Printf("###initClient--1")
}
func pushhandle(w http.ResponseWriter, r *http.Request, ps httprouter.Params) {
	fmt.Printf("query:%v", r.URL.Path)
	Contenttype := r.Header.Get("Content-Type")
	Authorization := r.Header.Get("Authorization")
	OrgId := r.Header.Get("X-Scope-OrgID")
	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		fmt.Printf("err:", err)
	}
	fmt.Printf("Contenttype:%v,Authorization:%v,body:%s", Contenttype, Authorization, body)
	if nil == send2prometheus(body) {
		w.WriteHeader(200)
	} else {
		w.WriteHeader(404)
	}
}
func marshtest() {
	for i := 0; i < 100; i++ {
		jsonStr := `
    {
        "wendu": 22.1,
        "shidu": 24.0
     }`
		send2prometheus([]byte(jsonStr))
		time.Sleep(5 * time.Second)
	}
}
func (c *conf) getConf() *conf {
	//应该是 绝对地址
	yamlFile, err := ioutil.ReadFile("./config.yaml")
	if err != nil {
		fmt.Println(err.Error())
	}

	err = yaml.Unmarshal(yamlFile, c)

	if err != nil {
		fmt.Println(err.Error())
	}

	return c
}
func main() {
	ctx, cancel = context.WithCancel(context.Background())
	defer cancel()
	var cfg conf
	//读取yaml配置文件
	config = cfg.getConf()
	fmt.Println(config)

	//将对象，转换成json格式
	data, err := json.Marshal(config)

	if err != nil {
		fmt.Println("err:\t", err.Error())
		return
	}

	//最终以json格式，输出
	fmt.Println("data:%v,backend:%v\t", string(data), config.Backend)

	fmt.Printf("###0")
	initClient()
	fmt.Printf("###1")
	subscribe()

	// go marshtest()
	// post server
	router := httprouter.New()
	router.POST("/easyapi/v1/push", pushhandle)

	var w sync.WaitGroup

	w.Add(1)
	go func() {
		defer w.Done()
		if err := http.ListenAndServe(":"+config.Http_port, router); err != nil {
			//if err := http.ListenAndServe("10.19.34.183:8899", nil); err != nil {
			// TODO need check return value
			fmt.Printf("listen up err:%v", err)
		}
		//logger.Log("listen up ok!")
	}()

	// detect quit signal

	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGHUP, syscall.SIGQUIT, syscall.SIGABRT, syscall.SIGKILL, syscall.SIGALRM, syscall.SIGUSR1, syscall.SIGUSR2, syscall.SIGTERM, syscall.SIGINT)

	w.Add(1)
	go func() {
		select {
		case sgName := <-ch:
			fmt.Printf("receive kill signal [%v], ready to exit ...", sgName)
		}
		fmt.Printf("###mqclient.Disconnect")
		// resource release and other deals
		mqclient.Disconnect(5)
		defer w.Done()
		os.Exit(0)
	}()
	w.Wait()
}
