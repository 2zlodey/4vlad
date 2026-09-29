//UDP Client
var dgram = require('dgram');
var client = dgram.createSocket('udp4');
var PORT = 2222;
var HOST='0.0.0.0';


var outbuf= new Buffer.from('!!!!!!!!!!!!!!!!!');
outbuf[0]=101;
frq=5000000;
//ttmp=frq-0xffffffff;
outbuf[1]= frq & 0xff;
outbuf[2]=(frq & 0xff00)>>8;
outbuf[3]=(frq & 0xff0000)>>16;
outbuf[4]=(frq & 0xff000000)>>24;
if (frq>0xffffffff) outbuf[5]=1; else outbuf[5]=0;
outbuf[6]=0;
outbuf[7]=0;
outbuf[8]=0;
/*
console.log(frq.toString(16),outbuf)
outbuf[3]=frq & 0xff;frq=frq>>8;
console.log(frq.toString(16),outbuf)
outbuf[2]=frq & 0xff;frq=frq>>8;
console.log(frq.toString(16),outbuf)
outbuf[1]=frq & 0xff;
*/


//console.log(outbuf)

client.on('message', (msg, remote) => {
  client.send(outbuf, 0, 2, 3333, remote.address);
  console.log(msg,' from ',remote)
  aaa=(msg[1]*256) + msg[0];
  if (msg[1]>127) aaa=-(0x10000-aaa);
  console.log(aaa/100)
});

//main
starttime = new Date();
//client.send(outbuf, 0, 9, PORT, HOST);
client.close;

client.bind(PORT, HOST);



//END
