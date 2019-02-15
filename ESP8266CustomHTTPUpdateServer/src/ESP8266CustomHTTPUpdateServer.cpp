#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include "StreamString.h"
#include "ESP8266CustomHTTPUpdateServer.h"


static const char serverIndex[] PROGMEM =
  R"(<!DOCTYPE html>
     <html lang="en">
     <head>
         <meta charset="UTF-8">
         <title>ESP8266 WebOTA</title>
         <style>
             body{text-align: center;height: 100%;}
             div,input{padding:5px;font-size:12px;}
             input{width:95%;margin-top: 5px;}
             button{padding:5px;border:0;border-radius:20px;background-color:#1fa3ec;color:#fff;line-height:30px;font-size:16px;width:100%;margin-top: 40px;}

             .m-user-icon{width: 100px;height: 100px;border-radius: 50px}
             .m-user-name{font-size: 18px;font-weight: bold;margin-top: 10px;}

             /*文件上传*/
             .fileupload{
                 position: relative;
                 width:150px;
                 height:25px;
                 border:1px solid #66B3FF;
                 border-radius: 4px;
                 box-shadow: 1px 1px 5px #66B3FF;
                 line-height: 25px;
                 overflow: hidden;
                 color: #66B3FF;;
                 left: 50%;
                 transform: translateX(-50%);
                 text-overflow:ellipsis;
                 white-space:nowrap
             }
             .fileupload input{
                 position: absolute;
                 width:150px;
                 height:25px;
                 top: 0;
                 left: 50%;
                 transform: translateX(-50%);
                 opacity: 0;
                 filter: alpha(opacity=0);
                 -ms-filter: 'alpha(opacity=0)';
             }

         </style>
     </head>
     <body>
     <div style="text-align:center;display:inline-block;min-width:260px;margin-top: 80px;">
       <div class="m-user">
           <img class= "m-user-icon" src="esp8266.png">
           <div class="m-user-name">ESP8266 WebOTA更新</div>
       </div>
       <form method='POST' action='' enctype='multipart/form-data'>
           <div class="fileupload">
               <script>
                   function getFilename(){
                       let filename=document.getElementById("file").value;
                       if(filename===undefined||filename===""){
                           document.getElementById("filename").innerHTML="点击此处上传文件";
                       } else{
                           let fn=filename.substring(filename.lastIndexOf("\\")+1);
                           document.getElementById("filename").innerHTML=fn; //将截取后的文件名填充到span中
                       }
                   }
               </script>
               <span id="filename">点击选择新固件</span>
               <input type="file" name="file" id="file" onchange="getFilename()"/>
           </div>
           <button type='submit'>确定更新</button>
       </form>
       <div style=";margin-top: 10px;">
           Copyright © 2019 By
           <a href='https://blog.csdn.net/wubo_fly'>单片机菜鸟</a>
       </div>
     </div>
     </body>
     </html>)";
static const char successResponse[] PROGMEM = 
  "<META http-equiv=\"refresh\" content=\"15;URL=/\">Update Success! Rebooting...\n";

ESP8266CustomHTTPUpdateServer::ESP8266CustomHTTPUpdateServer(bool serial_debug)
{
  _serial_output = serial_debug;
  _server = NULL;
  _username = NULL;
  _password = NULL;
  _authenticated = false;
}

void ESP8266CustomHTTPUpdateServer::setup(ESP8266WebServer *server, const char * path, const char * username, const char * password)
{
    _server = server;
    _username = (char *)username;
    _password = (char *)password;

    // handler for the /update form page
    _server->on(path, HTTP_GET, [&](){
      if(_username != NULL && _password != NULL && !_server->authenticate(_username, _password))
        return _server->requestAuthentication();
      _server->send_P(200, PSTR("text/html"), serverIndex);
    });

    // handler for the /update form POST (once file upload finishes)
    _server->on(path, HTTP_POST, [&](){
      if(!_authenticated)
        return _server->requestAuthentication();
      if (Update.hasError()) {
        _server->send(200, F("text/html"), String(F("Update error: ")) + _updaterError);
      } else {
        _server->client().setNoDelay(true);
        _server->send_P(200, PSTR("text/html"), successResponse);
        delay(100);
        _server->client().stop();
        ESP.restart();
      }
    },[&](){
      // handler for the file upload, get's the sketch bytes, and writes
      // them through the Update object
      HTTPUpload& upload = _server->upload();

      if(upload.status == UPLOAD_FILE_START){
        _updaterError = String();
        if (_serial_output)
          Serial.setDebugOutput(true);

        _authenticated = (_username == NULL || _password == NULL || _server->authenticate(_username, _password));
        if(!_authenticated){
          if (_serial_output)
            Serial.printf("Unauthenticated Update\n");
          return;
        }

        WiFiUDP::stopAll();
        if (_serial_output)
          Serial.printf("Update: %s\n", upload.filename.c_str());
        uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
        if(!Update.begin(maxSketchSpace)){//start with max available size
          _setUpdaterError();
        }
      } else if(_authenticated && upload.status == UPLOAD_FILE_WRITE && !_updaterError.length()){
        if (_serial_output) Serial.printf(".");
        if(Update.write(upload.buf, upload.currentSize) != upload.currentSize){
          _setUpdaterError();
        }
      } else if(_authenticated && upload.status == UPLOAD_FILE_END && !_updaterError.length()){
        if(Update.end(true)){ //true to set the size to the current progress
          if (_serial_output) Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
        } else {
          _setUpdaterError();
        }
        if (_serial_output) Serial.setDebugOutput(false);
      } else if(_authenticated && upload.status == UPLOAD_FILE_ABORTED){
        Update.end();
        if (_serial_output) Serial.println("Update was aborted");
      }
      delay(0);
    });
}

void ESP8266CustomHTTPUpdateServer::_setUpdaterError()
{
  if (_serial_output) Update.printError(Serial);
  StreamString str;
  Update.printError(str);
  _updaterError = str.c_str();
}
