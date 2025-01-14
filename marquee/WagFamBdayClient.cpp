/** The MIT License (MIT)

Copyright (c) 2023 Justin Wagner

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include "WagFamBdayClient.h"



WagFamBdayClient::WagFamBdayClient(String ApiKey, String JsonDataSourceUrl) {
  updateBdayClient(ApiKey, JsonDataSourceUrl);
}

void WagFamBdayClient::updateBdayClient(String ApiKey, String JsonDataSourceUrl) {
  myJsonSourceUrl = JsonDataSourceUrl;
  myApiKey = ApiKey;
}

WagFamBdayClient::configValues WagFamBdayClient::updateData() {
  currentConfig = {};
  JsonStreamingParser parser;
  parser.setListener(this);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(new BearSSL::WiFiClientSecure);
  client->setInsecure();

  HTTPClient https;

  Serial.println("Getting Birthdays Data");
  Serial.println(myJsonSourceUrl);

  if (!https.begin(*client, myJsonSourceUrl)) {
    Serial.println("[HTTPS] Unable to connect");
    return currentConfig;
  }

  // Add Authorization token if one is provided
  if (myApiKey != "") {
    https.addHeader("Authorization", "token " + myApiKey);
  }

  int httpCode = https.GET();

  if (httpCode < 0) {
    Serial.println("[HTTPS] GET... failed, error " + https.errorToString(httpCode));
    Serial.println();
    return currentConfig;
  }


  if(httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
    // get lenght of document (is -1 when Server sends no Content-Length header)
    int len = https.getSize();
    // create buffer for read
    char buff[128] = { 0 };
    // get tcp stream
    WiFiClient * stream = https.getStreamPtr();
    // read all data from server
    Serial.println("Start parsing...");
    while(https.connected() && (len > 0 || len == -1)) {
      // get available data size
      size_t size = stream->available();
      if(size) {
        // read up to 128 byte
        int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
        for(int i=0;i<c;i++) {
          parser.parse(buff[i]);
        }
        if(len > 0)
          len -= c;
        }
      delay(1);
    }
  } else {
    Serial.println("[HTTPS] GET... unsupported code: " + String(httpCode));
  }
  https.end();

  return currentConfig;
}

String WagFamBdayClient::getMessage(int index) {
  return messages[index];
}

int WagFamBdayClient::getNumMessages() {
  return messageCounter;
}

void WagFamBdayClient::updateDataSource(String JsonDataSourceUrl) {
  myJsonSourceUrl = JsonDataSourceUrl;
}

void WagFamBdayClient::whitespace(char c) {

}


// Here is the expected data format of the JSON we are fetching
//
// NOTE: not all config values need to be specified.  The client is smart enough to only update the
// particular configuration values that are present in the data, and not any others.
//
// [
//   {
//     "config": {
//       "dataSourceUrl": "",
//       "apiKey": "",
//       "eventToday": ""
//     }
//   },
//   {
//     "message": "Test Message 1"
//   },
//   {
//     "message": "Test Message 2"
//   }
// ]

void WagFamBdayClient::startDocument() {
  messageCounter = 0;
  inConfig = false;
}

void WagFamBdayClient::key(String key) {
  currentKey = key;
}

void WagFamBdayClient::value(String value) {
  if (inConfig) {
    // Here is where we save the config data from the server
    if (currentKey == "dataSourceUrl") {
      currentConfig.dataSourceUrlValid = true;
      currentConfig.dataSourceUrl = value;
    } else if (currentKey == "apiKey") {
      currentConfig.apiKeyValid = true;
      currentConfig.apiKey = value;
    } else if (currentKey == "eventToday") {
      currentConfig.eventTodayValid = true;
      currentConfig.eventToday = value.toInt();
    }
  } else if (currentKey == "message") {
    if (messageCounter >= 10) {
      // we are full so return
      return;
    }
    messages[messageCounter] = cleanText(value);
    messageCounter++;
  }

  Serial.println(currentKey + "=" + value);
}

void WagFamBdayClient::endArray() {
}

void WagFamBdayClient::endObject() {
  if (inConfig) {
    inConfig = false;
  }
}

void WagFamBdayClient::startArray() {
}

void WagFamBdayClient::startObject() {
  if (currentKey == "config") {
    inConfig = true;
  }
}

void WagFamBdayClient::endDocument() {
}

String WagFamBdayClient::cleanText(String text) {
  text.replace("’", "'");
  text.replace("“", "\"");
  text.replace("”", "\"");
  text.replace("`", "'");
  text.replace("‘", "'");
  text.replace("„", "'");
  text.replace("\\\"", "'");
  text.replace("•", "-");
  text.replace("é", "e");
  text.replace("è", "e");
  text.replace("ë", "e");
  text.replace("ê", "e");
  text.replace("à", "a");
  text.replace("â", "a");
  text.replace("ù", "u");
  text.replace("ç", "c");
  text.replace("î", "i");
  text.replace("ï", "i");
  text.replace("ô", "o");
  text.replace("…", "...");
  text.replace("–", "-");
  text.replace("Â", "A");
  text.replace("À", "A");
  text.replace("æ", "ae");
  text.replace("Æ", "AE");
  text.replace("É", "E");
  text.replace("È", "E");
  text.replace("Ë", "E");
  text.replace("Ô", "O");
  text.replace("Ö", "Oe");
  text.replace("ö", "oe");
  text.replace("œ", "oe");
  text.replace("Œ", "OE");
  text.replace("Ù", "U");
  text.replace("Û", "U");
  text.replace("Ü", "Ue");
  text.replace("ü", "ue");
  text.replace("Ä", "Ae");
  text.replace("ä", "ae");
  text.replace("ß", "ss");
  text.replace("»", "'");
  text.replace("«", "'");
  return text;
}
