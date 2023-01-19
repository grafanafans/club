package main

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/castai/promwrite"
	mqtt "github.com/eclipse/paho.mqtt.golang"
)

type dht11 struct {
	Wendu float32 `json:"wendu"`
	Shidu float32 `json:"shidu"`
}

var (
	promclient *promwrite.Client
	ctx        context.Context
	cancel     context.CancelFunc
	mqclient   mqtt.Client
)

func send2prometheus(data []byte) {
	tmp := dht11{}
	if err := json.Unmarshal([]byte(data), &tmp); err != nil {
		fmt.Printf("send2prometheus:err:%v", err)
	}
	fmt.Printf("send2prometheus,data:%v,wendu:%v,shidu:%v\n", string(data), tmp.Wendu, tmp.Shidu)
	now := time.Now().UTC()
	req := &promwrite.WriteRequest{
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
	}

	fmt.Printf("start to write\n")
	_, err := promclient.Write(ctx, req, promwrite.WriteHeaders(map[string]string{"Authorization": "abcdefxxx"}))
	if err != nil {
		fmt.Printf("send to prometheus err:%v", err)
	}
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
	mqclient.Subscribe("xapi/home/update", 0x00, subCallBackFunc)
}

func initClient() {
	promclient = promwrite.NewClient(
		"https://io.telemetrytower.com/api/v1/push",
		promwrite.HttpClient(&http.Client{
			Timeout: 30 * time.Second,
		}),
	)

	// init mqtt client
	opts := mqtt.NewClientOptions()

	// update for your emqx account
	opts.AddBroker("emqx.broker.host")
	opts.SetUsername("emqx.user.name")
	opts.SetPassword("emqx.user.password")

	mqclient = mqtt.NewClient(opts)
	if token := mqclient.Connect(); token.Wait() && token.Error() != nil {
		log.Panic("订阅 MQTT 失败")
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

func main() {
	ctx, cancel = context.WithCancel(context.Background())
	defer cancel()

	initClient()
	subscribe()

	// go marshtest()

	ch := make(chan os.Signal, 1)
	signal.Notify(ch, syscall.SIGHUP, syscall.SIGQUIT, syscall.SIGABRT, syscall.SIGKILL, syscall.SIGALRM, syscall.SIGUSR1, syscall.SIGUSR2, syscall.SIGTERM, syscall.SIGINT)

	var w sync.WaitGroup

	w.Add(1)
	go func() {
		select {
		case sgName := <-ch:
			fmt.Printf("receive kill signal [%v], ready to exit ...", sgName)
		}

		// resource release and other deals
		mqclient.Disconnect(5)
		defer w.Done()
	}()
	w.Wait()
}
