/*
 * Copyright 2019 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "symbol.h"
#include "value.h"
#include "expr.h"
#include "parser.h"
#include "utf8.h"
#include "lexint.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string>
#include <vector>
#include <iostream>
#include <utf8proc.h>

const char *symbolTable[] = {
  "ERROR", "ID", "OPERATOR", "LITERAL", "DEF", "VAL", "GLOBAL", "PUBLISH", "SUBSCRIBE", "PRIM", "LAMBDA",
  "DATA", "EQUALS", "POPEN", "PCLOSE", "BOPEN", "BCLOSE", "IF", "THEN", "ELSE", "HERE", "END",
  "MATCH", "EOL", "INDENT", "DEDENT", "COLON", "TARGET"
};

/*!re2c
  re2c:flags:tags = 1;
  re2c:tags:expression = "in.@@";
  re2c:define:YYCTYPE = "unsigned char";
  re2c:flags:8 = 1;
*/

/*!max:re2c*/
static const size_t SIZE = 4 * 1024;

/*!re2c
L = [\x41-\x5a\x61-\x7a\xaa-\xaa\xb5-\xb5\xba-\xba\xc0-\xd6\xd8-\xf6\xf8-\u02c1\u02c6-\u02d1\u02e0-\u02e4\u02ec-\u02ec\u02ee-\u02ee\u0370-\u0374\u0376-\u0377\u037a-\u037d\u037f-\u037f\u0386-\u0386\u0388-\u038a\u038c-\u038c\u038e-\u03a1\u03a3-\u03f5\u03f7-\u0481\u048a-\u052f\u0531-\u0556\u0559-\u0559\u0561-\u0587\u05d0-\u05ea\u05f0-\u05f2\u0620-\u064a\u066e-\u066f\u0671-\u06d3\u06d5-\u06d5\u06e5-\u06e6\u06ee-\u06ef\u06fa-\u06fc\u06ff-\u06ff\u0710-\u0710\u0712-\u072f\u074d-\u07a5\u07b1-\u07b1\u07ca-\u07ea\u07f4-\u07f5\u07fa-\u07fa\u0800-\u0815\u081a-\u081a\u0824-\u0824\u0828-\u0828\u0840-\u0858\u08a0-\u08b2\u0904-\u0939\u093d-\u093d\u0950-\u0950\u0958-\u0961\u0971-\u0980\u0985-\u098c\u098f-\u0990\u0993-\u09a8\u09aa-\u09b0\u09b2-\u09b2\u09b6-\u09b9\u09bd-\u09bd\u09ce-\u09ce\u09dc-\u09dd\u09df-\u09e1\u09f0-\u09f1\u0a05-\u0a0a\u0a0f-\u0a10\u0a13-\u0a28\u0a2a-\u0a30\u0a32-\u0a33\u0a35-\u0a36\u0a38-\u0a39\u0a59-\u0a5c\u0a5e-\u0a5e\u0a72-\u0a74\u0a85-\u0a8d\u0a8f-\u0a91\u0a93-\u0aa8\u0aaa-\u0ab0\u0ab2-\u0ab3\u0ab5-\u0ab9\u0abd-\u0abd\u0ad0-\u0ad0\u0ae0-\u0ae1\u0b05-\u0b0c\u0b0f-\u0b10\u0b13-\u0b28\u0b2a-\u0b30\u0b32-\u0b33\u0b35-\u0b39\u0b3d-\u0b3d\u0b5c-\u0b5d\u0b5f-\u0b61\u0b71-\u0b71\u0b83-\u0b83\u0b85-\u0b8a\u0b8e-\u0b90\u0b92-\u0b95\u0b99-\u0b9a\u0b9c-\u0b9c\u0b9e-\u0b9f\u0ba3-\u0ba4\u0ba8-\u0baa\u0bae-\u0bb9\u0bd0-\u0bd0\u0c05-\u0c0c\u0c0e-\u0c10\u0c12-\u0c28\u0c2a-\u0c39\u0c3d-\u0c3d\u0c58-\u0c59\u0c60-\u0c61\u0c85-\u0c8c\u0c8e-\u0c90\u0c92-\u0ca8\u0caa-\u0cb3\u0cb5-\u0cb9\u0cbd-\u0cbd\u0cde-\u0cde\u0ce0-\u0ce1\u0cf1-\u0cf2\u0d05-\u0d0c\u0d0e-\u0d10\u0d12-\u0d3a\u0d3d-\u0d3d\u0d4e-\u0d4e\u0d60-\u0d61\u0d7a-\u0d7f\u0d85-\u0d96\u0d9a-\u0db1\u0db3-\u0dbb\u0dbd-\u0dbd\u0dc0-\u0dc6\u0e01-\u0e30\u0e32-\u0e33\u0e40-\u0e46\u0e81-\u0e82\u0e84-\u0e84\u0e87-\u0e88\u0e8a-\u0e8a\u0e8d-\u0e8d\u0e94-\u0e97\u0e99-\u0e9f\u0ea1-\u0ea3\u0ea5-\u0ea5\u0ea7-\u0ea7\u0eaa-\u0eab\u0ead-\u0eb0\u0eb2-\u0eb3\u0ebd-\u0ebd\u0ec0-\u0ec4\u0ec6-\u0ec6\u0edc-\u0edf\u0f00-\u0f00\u0f40-\u0f47\u0f49-\u0f6c\u0f88-\u0f8c\u1000-\u102a\u103f-\u103f\u1050-\u1055\u105a-\u105d\u1061-\u1061\u1065-\u1066\u106e-\u1070\u1075-\u1081\u108e-\u108e\u10a0-\u10c5\u10c7-\u10c7\u10cd-\u10cd\u10d0-\u10fa\u10fc-\u1248\u124a-\u124d\u1250-\u1256\u1258-\u1258\u125a-\u125d\u1260-\u1288\u128a-\u128d\u1290-\u12b0\u12b2-\u12b5\u12b8-\u12be\u12c0-\u12c0\u12c2-\u12c5\u12c8-\u12d6\u12d8-\u1310\u1312-\u1315\u1318-\u135a\u1380-\u138f\u13a0-\u13f4\u1401-\u166c\u166f-\u167f\u1681-\u169a\u16a0-\u16ea\u16f1-\u16f8\u1700-\u170c\u170e-\u1711\u1720-\u1731\u1740-\u1751\u1760-\u176c\u176e-\u1770\u1780-\u17b3\u17d7-\u17d7\u17dc-\u17dc\u1820-\u1877\u1880-\u18a8\u18aa-\u18aa\u18b0-\u18f5\u1900-\u191e\u1950-\u196d\u1970-\u1974\u1980-\u19ab\u19c1-\u19c7\u1a00-\u1a16\u1a20-\u1a54\u1aa7-\u1aa7\u1b05-\u1b33\u1b45-\u1b4b\u1b83-\u1ba0\u1bae-\u1baf\u1bba-\u1be5\u1c00-\u1c23\u1c4d-\u1c4f\u1c5a-\u1c7d\u1ce9-\u1cec\u1cee-\u1cf1\u1cf5-\u1cf6\u1d00-\u1dbf\u1e00-\u1f15\u1f18-\u1f1d\u1f20-\u1f45\u1f48-\u1f4d\u1f50-\u1f57\u1f59-\u1f59\u1f5b-\u1f5b\u1f5d-\u1f5d\u1f5f-\u1f7d\u1f80-\u1fb4\u1fb6-\u1fbc\u1fbe-\u1fbe\u1fc2-\u1fc4\u1fc6-\u1fcc\u1fd0-\u1fd3\u1fd6-\u1fdb\u1fe0-\u1fec\u1ff2-\u1ff4\u1ff6-\u1ffc\u2071-\u2071\u207f-\u207f\u2090-\u209c\u2102-\u2102\u2107-\u2107\u210a-\u2113\u2115-\u2115\u2119-\u211d\u2124-\u2124\u2126-\u2126\u2128-\u2128\u212a-\u212d\u212f-\u2139\u213c-\u213f\u2145-\u2149\u214e-\u214e\u2183-\u2184\u2c00-\u2c2e\u2c30-\u2c5e\u2c60-\u2ce4\u2ceb-\u2cee\u2cf2-\u2cf3\u2d00-\u2d25\u2d27-\u2d27\u2d2d-\u2d2d\u2d30-\u2d67\u2d6f-\u2d6f\u2d80-\u2d96\u2da0-\u2da6\u2da8-\u2dae\u2db0-\u2db6\u2db8-\u2dbe\u2dc0-\u2dc6\u2dc8-\u2dce\u2dd0-\u2dd6\u2dd8-\u2dde\u2e2f-\u2e2f\u3005-\u3006\u3031-\u3035\u303b-\u303c\u3041-\u3096\u309d-\u309f\u30a1-\u30fa\u30fc-\u30ff\u3105-\u312d\u3131-\u318e\u31a0-\u31ba\u31f0-\u31ff\u3400-\u4db5\u4e00-\u9fcc\ua000-\ua48c\ua4d0-\ua4fd\ua500-\ua60c\ua610-\ua61f\ua62a-\ua62b\ua640-\ua66e\ua67f-\ua69d\ua6a0-\ua6e5\ua717-\ua71f\ua722-\ua788\ua78b-\ua78e\ua790-\ua7ad\ua7b0-\ua7b1\ua7f7-\ua801\ua803-\ua805\ua807-\ua80a\ua80c-\ua822\ua840-\ua873\ua882-\ua8b3\ua8f2-\ua8f7\ua8fb-\ua8fb\ua90a-\ua925\ua930-\ua946\ua960-\ua97c\ua984-\ua9b2\ua9cf-\ua9cf\ua9e0-\ua9e4\ua9e6-\ua9ef\ua9fa-\ua9fe\uaa00-\uaa28\uaa40-\uaa42\uaa44-\uaa4b\uaa60-\uaa76\uaa7a-\uaa7a\uaa7e-\uaaaf\uaab1-\uaab1\uaab5-\uaab6\uaab9-\uaabd\uaac0-\uaac0\uaac2-\uaac2\uaadb-\uaadd\uaae0-\uaaea\uaaf2-\uaaf4\uab01-\uab06\uab09-\uab0e\uab11-\uab16\uab20-\uab26\uab28-\uab2e\uab30-\uab5a\uab5c-\uab5f\uab64-\uab65\uabc0-\uabe2\uac00-\ud7a3\ud7b0-\ud7c6\ud7cb-\ud7fb\uf900-\ufa6d\ufa70-\ufad9\ufb00-\ufb06\ufb13-\ufb17\ufb1d-\ufb1d\ufb1f-\ufb28\ufb2a-\ufb36\ufb38-\ufb3c\ufb3e-\ufb3e\ufb40-\ufb41\ufb43-\ufb44\ufb46-\ufbb1\ufbd3-\ufd3d\ufd50-\ufd8f\ufd92-\ufdc7\ufdf0-\ufdfb\ufe70-\ufe74\ufe76-\ufefc\uff21-\uff3a\uff41-\uff5a\uff66-\uffbe\uffc2-\uffc7\uffca-\uffcf\uffd2-\uffd7\uffda-\uffdc\U00010000-\U0001000b\U0001000d-\U00010026\U00010028-\U0001003a\U0001003c-\U0001003d\U0001003f-\U0001004d\U00010050-\U0001005d\U00010080-\U000100fa\U00010280-\U0001029c\U000102a0-\U000102d0\U00010300-\U0001031f\U00010330-\U00010340\U00010342-\U00010349\U00010350-\U00010375\U00010380-\U0001039d\U000103a0-\U000103c3\U000103c8-\U000103cf\U00010400-\U0001049d\U00010500-\U00010527\U00010530-\U00010563\U00010600-\U00010736\U00010740-\U00010755\U00010760-\U00010767\U00010800-\U00010805\U00010808-\U00010808\U0001080a-\U00010835\U00010837-\U00010838\U0001083c-\U0001083c\U0001083f-\U00010855\U00010860-\U00010876\U00010880-\U0001089e\U00010900-\U00010915\U00010920-\U00010939\U00010980-\U000109b7\U000109be-\U000109bf\U00010a00-\U00010a00\U00010a10-\U00010a13\U00010a15-\U00010a17\U00010a19-\U00010a33\U00010a60-\U00010a7c\U00010a80-\U00010a9c\U00010ac0-\U00010ac7\U00010ac9-\U00010ae4\U00010b00-\U00010b35\U00010b40-\U00010b55\U00010b60-\U00010b72\U00010b80-\U00010b91\U00010c00-\U00010c48\U00011003-\U00011037\U00011083-\U000110af\U000110d0-\U000110e8\U00011103-\U00011126\U00011150-\U00011172\U00011176-\U00011176\U00011183-\U000111b2\U000111c1-\U000111c4\U000111da-\U000111da\U00011200-\U00011211\U00011213-\U0001122b\U000112b0-\U000112de\U00011305-\U0001130c\U0001130f-\U00011310\U00011313-\U00011328\U0001132a-\U00011330\U00011332-\U00011333\U00011335-\U00011339\U0001133d-\U0001133d\U0001135d-\U00011361\U00011480-\U000114af\U000114c4-\U000114c5\U000114c7-\U000114c7\U00011580-\U000115ae\U00011600-\U0001162f\U00011644-\U00011644\U00011680-\U000116aa\U000118a0-\U000118df\U000118ff-\U000118ff\U00011ac0-\U00011af8\U00012000-\U00012398\U00013000-\U0001342e\U00016800-\U00016a38\U00016a40-\U00016a5e\U00016ad0-\U00016aed\U00016b00-\U00016b2f\U00016b40-\U00016b43\U00016b63-\U00016b77\U00016b7d-\U00016b8f\U00016f00-\U00016f44\U00016f50-\U00016f50\U00016f93-\U00016f9f\U0001b000-\U0001b001\U0001bc00-\U0001bc6a\U0001bc70-\U0001bc7c\U0001bc80-\U0001bc88\U0001bc90-\U0001bc99\U0001d400-\U0001d454\U0001d456-\U0001d49c\U0001d49e-\U0001d49f\U0001d4a2-\U0001d4a2\U0001d4a5-\U0001d4a6\U0001d4a9-\U0001d4ac\U0001d4ae-\U0001d4b9\U0001d4bb-\U0001d4bb\U0001d4bd-\U0001d4c3\U0001d4c5-\U0001d505\U0001d507-\U0001d50a\U0001d50d-\U0001d514\U0001d516-\U0001d51c\U0001d51e-\U0001d539\U0001d53b-\U0001d53e\U0001d540-\U0001d544\U0001d546-\U0001d546\U0001d54a-\U0001d550\U0001d552-\U0001d6a5\U0001d6a8-\U0001d6c0\U0001d6c2-\U0001d6da\U0001d6dc-\U0001d6fa\U0001d6fc-\U0001d714\U0001d716-\U0001d734\U0001d736-\U0001d74e\U0001d750-\U0001d76e\U0001d770-\U0001d788\U0001d78a-\U0001d7a8\U0001d7aa-\U0001d7c2\U0001d7c4-\U0001d7cb\U0001e800-\U0001e8c4\U0001ee00-\U0001ee03\U0001ee05-\U0001ee1f\U0001ee21-\U0001ee22\U0001ee24-\U0001ee24\U0001ee27-\U0001ee27\U0001ee29-\U0001ee32\U0001ee34-\U0001ee37\U0001ee39-\U0001ee39\U0001ee3b-\U0001ee3b\U0001ee42-\U0001ee42\U0001ee47-\U0001ee47\U0001ee49-\U0001ee49\U0001ee4b-\U0001ee4b\U0001ee4d-\U0001ee4f\U0001ee51-\U0001ee52\U0001ee54-\U0001ee54\U0001ee57-\U0001ee57\U0001ee59-\U0001ee59\U0001ee5b-\U0001ee5b\U0001ee5d-\U0001ee5d\U0001ee5f-\U0001ee5f\U0001ee61-\U0001ee62\U0001ee64-\U0001ee64\U0001ee67-\U0001ee6a\U0001ee6c-\U0001ee72\U0001ee74-\U0001ee77\U0001ee79-\U0001ee7c\U0001ee7e-\U0001ee7e\U0001ee80-\U0001ee89\U0001ee8b-\U0001ee9b\U0001eea1-\U0001eea3\U0001eea5-\U0001eea9\U0001eeab-\U0001eebb\U00020000-\U0002a6d6\U0002a700-\U0002b734\U0002b740-\U0002b81d\U0002f800-\U0002fa1d];
Lu = [\x41-\x5a\xc0-\xd6\xd8-\xde\u0100-\u0100\u0102-\u0102\u0104-\u0104\u0106-\u0106\u0108-\u0108\u010a-\u010a\u010c-\u010c\u010e-\u010e\u0110-\u0110\u0112-\u0112\u0114-\u0114\u0116-\u0116\u0118-\u0118\u011a-\u011a\u011c-\u011c\u011e-\u011e\u0120-\u0120\u0122-\u0122\u0124-\u0124\u0126-\u0126\u0128-\u0128\u012a-\u012a\u012c-\u012c\u012e-\u012e\u0130-\u0130\u0132-\u0132\u0134-\u0134\u0136-\u0136\u0139-\u0139\u013b-\u013b\u013d-\u013d\u013f-\u013f\u0141-\u0141\u0143-\u0143\u0145-\u0145\u0147-\u0147\u014a-\u014a\u014c-\u014c\u014e-\u014e\u0150-\u0150\u0152-\u0152\u0154-\u0154\u0156-\u0156\u0158-\u0158\u015a-\u015a\u015c-\u015c\u015e-\u015e\u0160-\u0160\u0162-\u0162\u0164-\u0164\u0166-\u0166\u0168-\u0168\u016a-\u016a\u016c-\u016c\u016e-\u016e\u0170-\u0170\u0172-\u0172\u0174-\u0174\u0176-\u0176\u0178-\u0179\u017b-\u017b\u017d-\u017d\u0181-\u0182\u0184-\u0184\u0186-\u0187\u0189-\u018b\u018e-\u0191\u0193-\u0194\u0196-\u0198\u019c-\u019d\u019f-\u01a0\u01a2-\u01a2\u01a4-\u01a4\u01a6-\u01a7\u01a9-\u01a9\u01ac-\u01ac\u01ae-\u01af\u01b1-\u01b3\u01b5-\u01b5\u01b7-\u01b8\u01bc-\u01bc\u01c4-\u01c4\u01c7-\u01c7\u01ca-\u01ca\u01cd-\u01cd\u01cf-\u01cf\u01d1-\u01d1\u01d3-\u01d3\u01d5-\u01d5\u01d7-\u01d7\u01d9-\u01d9\u01db-\u01db\u01de-\u01de\u01e0-\u01e0\u01e2-\u01e2\u01e4-\u01e4\u01e6-\u01e6\u01e8-\u01e8\u01ea-\u01ea\u01ec-\u01ec\u01ee-\u01ee\u01f1-\u01f1\u01f4-\u01f4\u01f6-\u01f8\u01fa-\u01fa\u01fc-\u01fc\u01fe-\u01fe\u0200-\u0200\u0202-\u0202\u0204-\u0204\u0206-\u0206\u0208-\u0208\u020a-\u020a\u020c-\u020c\u020e-\u020e\u0210-\u0210\u0212-\u0212\u0214-\u0214\u0216-\u0216\u0218-\u0218\u021a-\u021a\u021c-\u021c\u021e-\u021e\u0220-\u0220\u0222-\u0222\u0224-\u0224\u0226-\u0226\u0228-\u0228\u022a-\u022a\u022c-\u022c\u022e-\u022e\u0230-\u0230\u0232-\u0232\u023a-\u023b\u023d-\u023e\u0241-\u0241\u0243-\u0246\u0248-\u0248\u024a-\u024a\u024c-\u024c\u024e-\u024e\u0370-\u0370\u0372-\u0372\u0376-\u0376\u037f-\u037f\u0386-\u0386\u0388-\u038a\u038c-\u038c\u038e-\u038f\u0391-\u03a1\u03a3-\u03ab\u03cf-\u03cf\u03d2-\u03d4\u03d8-\u03d8\u03da-\u03da\u03dc-\u03dc\u03de-\u03de\u03e0-\u03e0\u03e2-\u03e2\u03e4-\u03e4\u03e6-\u03e6\u03e8-\u03e8\u03ea-\u03ea\u03ec-\u03ec\u03ee-\u03ee\u03f4-\u03f4\u03f7-\u03f7\u03f9-\u03fa\u03fd-\u042f\u0460-\u0460\u0462-\u0462\u0464-\u0464\u0466-\u0466\u0468-\u0468\u046a-\u046a\u046c-\u046c\u046e-\u046e\u0470-\u0470\u0472-\u0472\u0474-\u0474\u0476-\u0476\u0478-\u0478\u047a-\u047a\u047c-\u047c\u047e-\u047e\u0480-\u0480\u048a-\u048a\u048c-\u048c\u048e-\u048e\u0490-\u0490\u0492-\u0492\u0494-\u0494\u0496-\u0496\u0498-\u0498\u049a-\u049a\u049c-\u049c\u049e-\u049e\u04a0-\u04a0\u04a2-\u04a2\u04a4-\u04a4\u04a6-\u04a6\u04a8-\u04a8\u04aa-\u04aa\u04ac-\u04ac\u04ae-\u04ae\u04b0-\u04b0\u04b2-\u04b2\u04b4-\u04b4\u04b6-\u04b6\u04b8-\u04b8\u04ba-\u04ba\u04bc-\u04bc\u04be-\u04be\u04c0-\u04c1\u04c3-\u04c3\u04c5-\u04c5\u04c7-\u04c7\u04c9-\u04c9\u04cb-\u04cb\u04cd-\u04cd\u04d0-\u04d0\u04d2-\u04d2\u04d4-\u04d4\u04d6-\u04d6\u04d8-\u04d8\u04da-\u04da\u04dc-\u04dc\u04de-\u04de\u04e0-\u04e0\u04e2-\u04e2\u04e4-\u04e4\u04e6-\u04e6\u04e8-\u04e8\u04ea-\u04ea\u04ec-\u04ec\u04ee-\u04ee\u04f0-\u04f0\u04f2-\u04f2\u04f4-\u04f4\u04f6-\u04f6\u04f8-\u04f8\u04fa-\u04fa\u04fc-\u04fc\u04fe-\u04fe\u0500-\u0500\u0502-\u0502\u0504-\u0504\u0506-\u0506\u0508-\u0508\u050a-\u050a\u050c-\u050c\u050e-\u050e\u0510-\u0510\u0512-\u0512\u0514-\u0514\u0516-\u0516\u0518-\u0518\u051a-\u051a\u051c-\u051c\u051e-\u051e\u0520-\u0520\u0522-\u0522\u0524-\u0524\u0526-\u0526\u0528-\u0528\u052a-\u052a\u052c-\u052c\u052e-\u052e\u0531-\u0556\u10a0-\u10c5\u10c7-\u10c7\u10cd-\u10cd\u1e00-\u1e00\u1e02-\u1e02\u1e04-\u1e04\u1e06-\u1e06\u1e08-\u1e08\u1e0a-\u1e0a\u1e0c-\u1e0c\u1e0e-\u1e0e\u1e10-\u1e10\u1e12-\u1e12\u1e14-\u1e14\u1e16-\u1e16\u1e18-\u1e18\u1e1a-\u1e1a\u1e1c-\u1e1c\u1e1e-\u1e1e\u1e20-\u1e20\u1e22-\u1e22\u1e24-\u1e24\u1e26-\u1e26\u1e28-\u1e28\u1e2a-\u1e2a\u1e2c-\u1e2c\u1e2e-\u1e2e\u1e30-\u1e30\u1e32-\u1e32\u1e34-\u1e34\u1e36-\u1e36\u1e38-\u1e38\u1e3a-\u1e3a\u1e3c-\u1e3c\u1e3e-\u1e3e\u1e40-\u1e40\u1e42-\u1e42\u1e44-\u1e44\u1e46-\u1e46\u1e48-\u1e48\u1e4a-\u1e4a\u1e4c-\u1e4c\u1e4e-\u1e4e\u1e50-\u1e50\u1e52-\u1e52\u1e54-\u1e54\u1e56-\u1e56\u1e58-\u1e58\u1e5a-\u1e5a\u1e5c-\u1e5c\u1e5e-\u1e5e\u1e60-\u1e60\u1e62-\u1e62\u1e64-\u1e64\u1e66-\u1e66\u1e68-\u1e68\u1e6a-\u1e6a\u1e6c-\u1e6c\u1e6e-\u1e6e\u1e70-\u1e70\u1e72-\u1e72\u1e74-\u1e74\u1e76-\u1e76\u1e78-\u1e78\u1e7a-\u1e7a\u1e7c-\u1e7c\u1e7e-\u1e7e\u1e80-\u1e80\u1e82-\u1e82\u1e84-\u1e84\u1e86-\u1e86\u1e88-\u1e88\u1e8a-\u1e8a\u1e8c-\u1e8c\u1e8e-\u1e8e\u1e90-\u1e90\u1e92-\u1e92\u1e94-\u1e94\u1e9e-\u1e9e\u1ea0-\u1ea0\u1ea2-\u1ea2\u1ea4-\u1ea4\u1ea6-\u1ea6\u1ea8-\u1ea8\u1eaa-\u1eaa\u1eac-\u1eac\u1eae-\u1eae\u1eb0-\u1eb0\u1eb2-\u1eb2\u1eb4-\u1eb4\u1eb6-\u1eb6\u1eb8-\u1eb8\u1eba-\u1eba\u1ebc-\u1ebc\u1ebe-\u1ebe\u1ec0-\u1ec0\u1ec2-\u1ec2\u1ec4-\u1ec4\u1ec6-\u1ec6\u1ec8-\u1ec8\u1eca-\u1eca\u1ecc-\u1ecc\u1ece-\u1ece\u1ed0-\u1ed0\u1ed2-\u1ed2\u1ed4-\u1ed4\u1ed6-\u1ed6\u1ed8-\u1ed8\u1eda-\u1eda\u1edc-\u1edc\u1ede-\u1ede\u1ee0-\u1ee0\u1ee2-\u1ee2\u1ee4-\u1ee4\u1ee6-\u1ee6\u1ee8-\u1ee8\u1eea-\u1eea\u1eec-\u1eec\u1eee-\u1eee\u1ef0-\u1ef0\u1ef2-\u1ef2\u1ef4-\u1ef4\u1ef6-\u1ef6\u1ef8-\u1ef8\u1efa-\u1efa\u1efc-\u1efc\u1efe-\u1efe\u1f08-\u1f0f\u1f18-\u1f1d\u1f28-\u1f2f\u1f38-\u1f3f\u1f48-\u1f4d\u1f59-\u1f59\u1f5b-\u1f5b\u1f5d-\u1f5d\u1f5f-\u1f5f\u1f68-\u1f6f\u1fb8-\u1fbb\u1fc8-\u1fcb\u1fd8-\u1fdb\u1fe8-\u1fec\u1ff8-\u1ffb\u2102-\u2102\u2107-\u2107\u210b-\u210d\u2110-\u2112\u2115-\u2115\u2119-\u211d\u2124-\u2124\u2126-\u2126\u2128-\u2128\u212a-\u212d\u2130-\u2133\u213e-\u213f\u2145-\u2145\u2183-\u2183\u2c00-\u2c2e\u2c60-\u2c60\u2c62-\u2c64\u2c67-\u2c67\u2c69-\u2c69\u2c6b-\u2c6b\u2c6d-\u2c70\u2c72-\u2c72\u2c75-\u2c75\u2c7e-\u2c80\u2c82-\u2c82\u2c84-\u2c84\u2c86-\u2c86\u2c88-\u2c88\u2c8a-\u2c8a\u2c8c-\u2c8c\u2c8e-\u2c8e\u2c90-\u2c90\u2c92-\u2c92\u2c94-\u2c94\u2c96-\u2c96\u2c98-\u2c98\u2c9a-\u2c9a\u2c9c-\u2c9c\u2c9e-\u2c9e\u2ca0-\u2ca0\u2ca2-\u2ca2\u2ca4-\u2ca4\u2ca6-\u2ca6\u2ca8-\u2ca8\u2caa-\u2caa\u2cac-\u2cac\u2cae-\u2cae\u2cb0-\u2cb0\u2cb2-\u2cb2\u2cb4-\u2cb4\u2cb6-\u2cb6\u2cb8-\u2cb8\u2cba-\u2cba\u2cbc-\u2cbc\u2cbe-\u2cbe\u2cc0-\u2cc0\u2cc2-\u2cc2\u2cc4-\u2cc4\u2cc6-\u2cc6\u2cc8-\u2cc8\u2cca-\u2cca\u2ccc-\u2ccc\u2cce-\u2cce\u2cd0-\u2cd0\u2cd2-\u2cd2\u2cd4-\u2cd4\u2cd6-\u2cd6\u2cd8-\u2cd8\u2cda-\u2cda\u2cdc-\u2cdc\u2cde-\u2cde\u2ce0-\u2ce0\u2ce2-\u2ce2\u2ceb-\u2ceb\u2ced-\u2ced\u2cf2-\u2cf2\ua640-\ua640\ua642-\ua642\ua644-\ua644\ua646-\ua646\ua648-\ua648\ua64a-\ua64a\ua64c-\ua64c\ua64e-\ua64e\ua650-\ua650\ua652-\ua652\ua654-\ua654\ua656-\ua656\ua658-\ua658\ua65a-\ua65a\ua65c-\ua65c\ua65e-\ua65e\ua660-\ua660\ua662-\ua662\ua664-\ua664\ua666-\ua666\ua668-\ua668\ua66a-\ua66a\ua66c-\ua66c\ua680-\ua680\ua682-\ua682\ua684-\ua684\ua686-\ua686\ua688-\ua688\ua68a-\ua68a\ua68c-\ua68c\ua68e-\ua68e\ua690-\ua690\ua692-\ua692\ua694-\ua694\ua696-\ua696\ua698-\ua698\ua69a-\ua69a\ua722-\ua722\ua724-\ua724\ua726-\ua726\ua728-\ua728\ua72a-\ua72a\ua72c-\ua72c\ua72e-\ua72e\ua732-\ua732\ua734-\ua734\ua736-\ua736\ua738-\ua738\ua73a-\ua73a\ua73c-\ua73c\ua73e-\ua73e\ua740-\ua740\ua742-\ua742\ua744-\ua744\ua746-\ua746\ua748-\ua748\ua74a-\ua74a\ua74c-\ua74c\ua74e-\ua74e\ua750-\ua750\ua752-\ua752\ua754-\ua754\ua756-\ua756\ua758-\ua758\ua75a-\ua75a\ua75c-\ua75c\ua75e-\ua75e\ua760-\ua760\ua762-\ua762\ua764-\ua764\ua766-\ua766\ua768-\ua768\ua76a-\ua76a\ua76c-\ua76c\ua76e-\ua76e\ua779-\ua779\ua77b-\ua77b\ua77d-\ua77e\ua780-\ua780\ua782-\ua782\ua784-\ua784\ua786-\ua786\ua78b-\ua78b\ua78d-\ua78d\ua790-\ua790\ua792-\ua792\ua796-\ua796\ua798-\ua798\ua79a-\ua79a\ua79c-\ua79c\ua79e-\ua79e\ua7a0-\ua7a0\ua7a2-\ua7a2\ua7a4-\ua7a4\ua7a6-\ua7a6\ua7a8-\ua7a8\ua7aa-\ua7ad\ua7b0-\ua7b1\uff21-\uff3a\U00010400-\U00010427\U000118a0-\U000118bf\U0001d400-\U0001d419\U0001d434-\U0001d44d\U0001d468-\U0001d481\U0001d49c-\U0001d49c\U0001d49e-\U0001d49f\U0001d4a2-\U0001d4a2\U0001d4a5-\U0001d4a6\U0001d4a9-\U0001d4ac\U0001d4ae-\U0001d4b5\U0001d4d0-\U0001d4e9\U0001d504-\U0001d505\U0001d507-\U0001d50a\U0001d50d-\U0001d514\U0001d516-\U0001d51c\U0001d538-\U0001d539\U0001d53b-\U0001d53e\U0001d540-\U0001d544\U0001d546-\U0001d546\U0001d54a-\U0001d550\U0001d56c-\U0001d585\U0001d5a0-\U0001d5b9\U0001d5d4-\U0001d5ed\U0001d608-\U0001d621\U0001d63c-\U0001d655\U0001d670-\U0001d689\U0001d6a8-\U0001d6c0\U0001d6e2-\U0001d6fa\U0001d71c-\U0001d734\U0001d756-\U0001d76e\U0001d790-\U0001d7a8\U0001d7ca-\U0001d7ca];
Lt = [\u01c5-\u01c5\u01c8-\u01c8\u01cb-\u01cb\u01f2-\u01f2\u1f88-\u1f8f\u1f98-\u1f9f\u1fa8-\u1faf\u1fbc-\u1fbc\u1fcc-\u1fcc\u1ffc-\u1ffc];
Lm = [\u02b0-\u02c1\u02c6-\u02d1\u02e0-\u02e4\u02ec-\u02ec\u02ee-\u02ee\u0374-\u0374\u037a-\u037a\u0559-\u0559\u0640-\u0640\u06e5-\u06e6\u07f4-\u07f5\u07fa-\u07fa\u081a-\u081a\u0824-\u0824\u0828-\u0828\u0971-\u0971\u0e46-\u0e46\u0ec6-\u0ec6\u10fc-\u10fc\u17d7-\u17d7\u1843-\u1843\u1aa7-\u1aa7\u1c78-\u1c7d\u1d2c-\u1d6a\u1d78-\u1d78\u1d9b-\u1dbf\u2071-\u2071\u207f-\u207f\u2090-\u209c\u2c7c-\u2c7d\u2d6f-\u2d6f\u2e2f-\u2e2f\u3005-\u3005\u3031-\u3035\u303b-\u303b\u309d-\u309e\u30fc-\u30fe\ua015-\ua015\ua4f8-\ua4fd\ua60c-\ua60c\ua67f-\ua67f\ua69c-\ua69d\ua717-\ua71f\ua770-\ua770\ua788-\ua788\ua7f8-\ua7f9\ua9cf-\ua9cf\ua9e6-\ua9e6\uaa70-\uaa70\uaadd-\uaadd\uaaf3-\uaaf4\uab5c-\uab5f\uff70-\uff70\uff9e-\uff9f\U00016b40-\U00016b43\U00016f93-\U00016f9f];
M = [\u0300-\u036f\u0483-\u0489\u0591-\u05bd\u05bf-\u05bf\u05c1-\u05c2\u05c4-\u05c5\u05c7-\u05c7\u0610-\u061a\u064b-\u065f\u0670-\u0670\u06d6-\u06dc\u06df-\u06e4\u06e7-\u06e8\u06ea-\u06ed\u0711-\u0711\u0730-\u074a\u07a6-\u07b0\u07eb-\u07f3\u0816-\u0819\u081b-\u0823\u0825-\u0827\u0829-\u082d\u0859-\u085b\u08e4-\u0903\u093a-\u093c\u093e-\u094f\u0951-\u0957\u0962-\u0963\u0981-\u0983\u09bc-\u09bc\u09be-\u09c4\u09c7-\u09c8\u09cb-\u09cd\u09d7-\u09d7\u09e2-\u09e3\u0a01-\u0a03\u0a3c-\u0a3c\u0a3e-\u0a42\u0a47-\u0a48\u0a4b-\u0a4d\u0a51-\u0a51\u0a70-\u0a71\u0a75-\u0a75\u0a81-\u0a83\u0abc-\u0abc\u0abe-\u0ac5\u0ac7-\u0ac9\u0acb-\u0acd\u0ae2-\u0ae3\u0b01-\u0b03\u0b3c-\u0b3c\u0b3e-\u0b44\u0b47-\u0b48\u0b4b-\u0b4d\u0b56-\u0b57\u0b62-\u0b63\u0b82-\u0b82\u0bbe-\u0bc2\u0bc6-\u0bc8\u0bca-\u0bcd\u0bd7-\u0bd7\u0c00-\u0c03\u0c3e-\u0c44\u0c46-\u0c48\u0c4a-\u0c4d\u0c55-\u0c56\u0c62-\u0c63\u0c81-\u0c83\u0cbc-\u0cbc\u0cbe-\u0cc4\u0cc6-\u0cc8\u0cca-\u0ccd\u0cd5-\u0cd6\u0ce2-\u0ce3\u0d01-\u0d03\u0d3e-\u0d44\u0d46-\u0d48\u0d4a-\u0d4d\u0d57-\u0d57\u0d62-\u0d63\u0d82-\u0d83\u0dca-\u0dca\u0dcf-\u0dd4\u0dd6-\u0dd6\u0dd8-\u0ddf\u0df2-\u0df3\u0e31-\u0e31\u0e34-\u0e3a\u0e47-\u0e4e\u0eb1-\u0eb1\u0eb4-\u0eb9\u0ebb-\u0ebc\u0ec8-\u0ecd\u0f18-\u0f19\u0f35-\u0f35\u0f37-\u0f37\u0f39-\u0f39\u0f3e-\u0f3f\u0f71-\u0f84\u0f86-\u0f87\u0f8d-\u0f97\u0f99-\u0fbc\u0fc6-\u0fc6\u102b-\u103e\u1056-\u1059\u105e-\u1060\u1062-\u1064\u1067-\u106d\u1071-\u1074\u1082-\u108d\u108f-\u108f\u109a-\u109d\u135d-\u135f\u1712-\u1714\u1732-\u1734\u1752-\u1753\u1772-\u1773\u17b4-\u17d3\u17dd-\u17dd\u180b-\u180d\u18a9-\u18a9\u1920-\u192b\u1930-\u193b\u19b0-\u19c0\u19c8-\u19c9\u1a17-\u1a1b\u1a55-\u1a5e\u1a60-\u1a7c\u1a7f-\u1a7f\u1ab0-\u1abe\u1b00-\u1b04\u1b34-\u1b44\u1b6b-\u1b73\u1b80-\u1b82\u1ba1-\u1bad\u1be6-\u1bf3\u1c24-\u1c37\u1cd0-\u1cd2\u1cd4-\u1ce8\u1ced-\u1ced\u1cf2-\u1cf4\u1cf8-\u1cf9\u1dc0-\u1df5\u1dfc-\u1dff\u20d0-\u20f0\u2cef-\u2cf1\u2d7f-\u2d7f\u2de0-\u2dff\u302a-\u302f\u3099-\u309a\ua66f-\ua672\ua674-\ua67d\ua69f-\ua69f\ua6f0-\ua6f1\ua802-\ua802\ua806-\ua806\ua80b-\ua80b\ua823-\ua827\ua880-\ua881\ua8b4-\ua8c4\ua8e0-\ua8f1\ua926-\ua92d\ua947-\ua953\ua980-\ua983\ua9b3-\ua9c0\ua9e5-\ua9e5\uaa29-\uaa36\uaa43-\uaa43\uaa4c-\uaa4d\uaa7b-\uaa7d\uaab0-\uaab0\uaab2-\uaab4\uaab7-\uaab8\uaabe-\uaabf\uaac1-\uaac1\uaaeb-\uaaef\uaaf5-\uaaf6\uabe3-\uabea\uabec-\uabed\ufb1e-\ufb1e\ufe00-\ufe0f\ufe20-\ufe2d\U000101fd-\U000101fd\U000102e0-\U000102e0\U00010376-\U0001037a\U00010a01-\U00010a03\U00010a05-\U00010a06\U00010a0c-\U00010a0f\U00010a38-\U00010a3a\U00010a3f-\U00010a3f\U00010ae5-\U00010ae6\U00011000-\U00011002\U00011038-\U00011046\U0001107f-\U00011082\U000110b0-\U000110ba\U00011100-\U00011102\U00011127-\U00011134\U00011173-\U00011173\U00011180-\U00011182\U000111b3-\U000111c0\U0001122c-\U00011237\U000112df-\U000112ea\U00011301-\U00011303\U0001133c-\U0001133c\U0001133e-\U00011344\U00011347-\U00011348\U0001134b-\U0001134d\U00011357-\U00011357\U00011362-\U00011363\U00011366-\U0001136c\U00011370-\U00011374\U000114b0-\U000114c3\U000115af-\U000115b5\U000115b8-\U000115c0\U00011630-\U00011640\U000116ab-\U000116b7\U00016af0-\U00016af4\U00016b30-\U00016b36\U00016f51-\U00016f7e\U00016f8f-\U00016f92\U0001bc9d-\U0001bc9e\U0001d165-\U0001d169\U0001d16d-\U0001d172\U0001d17b-\U0001d182\U0001d185-\U0001d18b\U0001d1aa-\U0001d1ad\U0001d242-\U0001d244\U0001e8d0-\U0001e8d6\U000e0100-\U000e01ef];
Sc = [\x24-\x24\xa2-\xa5\u058f-\u058f\u060b-\u060b\u09f2-\u09f3\u09fb-\u09fb\u0af1-\u0af1\u0bf9-\u0bf9\u0e3f-\u0e3f\u17db-\u17db\u20a0-\u20bd\ua838-\ua838\ufdfc-\ufdfc\ufe69-\ufe69\uff04-\uff04\uffe0-\uffe1\uffe5-\uffe6];
Sk = [\x5e-\x5e\x60-\x60\xa8-\xa8\xaf-\xaf\xb4-\xb4\xb8-\xb8\u02c2-\u02c5\u02d2-\u02df\u02e5-\u02eb\u02ed-\u02ed\u02ef-\u02ff\u0375-\u0375\u0384-\u0385\u1fbd-\u1fbd\u1fbf-\u1fc1\u1fcd-\u1fcf\u1fdd-\u1fdf\u1fed-\u1fef\u1ffd-\u1ffe\u309b-\u309c\ua700-\ua716\ua720-\ua721\ua789-\ua78a\uab5b-\uab5b\ufbb2-\ufbc1\uff3e-\uff3e\uff40-\uff40\uffe3-\uffe3];
Sk_notick = [\x5e-\x5e\xa8-\xa8\xaf-\xaf\xb4-\xb4\xb8-\xb8\u02c2-\u02c5\u02d2-\u02df\u02e5-\u02eb\u02ed-\u02ed\u02ef-\u02ff\u0375-\u0375\u0384-\u0385\u1fbd-\u1fbd\u1fbf-\u1fc1\u1fcd-\u1fcf\u1fdd-\u1fdf\u1fed-\u1fef\u1ffd-\u1ffe\u309b-\u309c\ua700-\ua716\ua720-\ua721\ua789-\ua78a\uab5b-\uab5b\ufbb2-\ufbc1\uff3e-\uff3e\uff40-\uff40\uffe3-\uffe3];
So = [\xa6-\xa6\xa9-\xa9\xae-\xae\xb0-\xb0\u0482-\u0482\u058d-\u058e\u060e-\u060f\u06de-\u06de\u06e9-\u06e9\u06fd-\u06fe\u07f6-\u07f6\u09fa-\u09fa\u0b70-\u0b70\u0bf3-\u0bf8\u0bfa-\u0bfa\u0c7f-\u0c7f\u0d79-\u0d79\u0f01-\u0f03\u0f13-\u0f13\u0f15-\u0f17\u0f1a-\u0f1f\u0f34-\u0f34\u0f36-\u0f36\u0f38-\u0f38\u0fbe-\u0fc5\u0fc7-\u0fcc\u0fce-\u0fcf\u0fd5-\u0fd8\u109e-\u109f\u1390-\u1399\u1940-\u1940\u19de-\u19ff\u1b61-\u1b6a\u1b74-\u1b7c\u2100-\u2101\u2103-\u2106\u2108-\u2109\u2114-\u2114\u2116-\u2117\u211e-\u2123\u2125-\u2125\u2127-\u2127\u2129-\u2129\u212e-\u212e\u213a-\u213b\u214a-\u214a\u214c-\u214d\u214f-\u214f\u2195-\u2199\u219c-\u219f\u21a1-\u21a2\u21a4-\u21a5\u21a7-\u21ad\u21af-\u21cd\u21d0-\u21d1\u21d3-\u21d3\u21d5-\u21f3\u2300-\u2307\u230c-\u231f\u2322-\u2328\u232b-\u237b\u237d-\u239a\u23b4-\u23db\u23e2-\u23fa\u2400-\u2426\u2440-\u244a\u249c-\u24e9\u2500-\u25b6\u25b8-\u25c0\u25c2-\u25f7\u2600-\u266e\u2670-\u2767\u2794-\u27bf\u2800-\u28ff\u2b00-\u2b2f\u2b45-\u2b46\u2b4d-\u2b73\u2b76-\u2b95\u2b98-\u2bb9\u2bbd-\u2bc8\u2bca-\u2bd1\u2ce5-\u2cea\u2e80-\u2e99\u2e9b-\u2ef3\u2f00-\u2fd5\u2ff0-\u2ffb\u3004-\u3004\u3012-\u3013\u3020-\u3020\u3036-\u3037\u303e-\u303f\u3190-\u3191\u3196-\u319f\u31c0-\u31e3\u3200-\u321e\u322a-\u3247\u3250-\u3250\u3260-\u327f\u328a-\u32b0\u32c0-\u32fe\u3300-\u33ff\u4dc0-\u4dff\ua490-\ua4c6\ua828-\ua82b\ua836-\ua837\ua839-\ua839\uaa77-\uaa79\ufdfd-\ufdfd\uffe4-\uffe4\uffe8-\uffe8\uffed-\uffee\ufffc-\ufffd\U00010137-\U0001013f\U00010179-\U00010189\U0001018c-\U0001018c\U00010190-\U0001019b\U000101a0-\U000101a0\U000101d0-\U000101fc\U00010877-\U00010878\U00010ac8-\U00010ac8\U00016b3c-\U00016b3f\U00016b45-\U00016b45\U0001bc9c-\U0001bc9c\U0001d000-\U0001d0f5\U0001d100-\U0001d126\U0001d129-\U0001d164\U0001d16a-\U0001d16c\U0001d183-\U0001d184\U0001d18c-\U0001d1a9\U0001d1ae-\U0001d1dd\U0001d200-\U0001d241\U0001d245-\U0001d245\U0001d300-\U0001d356\U0001f000-\U0001f02b\U0001f030-\U0001f093\U0001f0a0-\U0001f0ae\U0001f0b1-\U0001f0bf\U0001f0c1-\U0001f0cf\U0001f0d1-\U0001f0f5\U0001f110-\U0001f12e\U0001f130-\U0001f16b\U0001f170-\U0001f19a\U0001f1e6-\U0001f202\U0001f210-\U0001f23a\U0001f240-\U0001f248\U0001f250-\U0001f251\U0001f300-\U0001f32c\U0001f330-\U0001f37d\U0001f380-\U0001f3ce\U0001f3d4-\U0001f3f7\U0001f400-\U0001f4fe\U0001f500-\U0001f54a\U0001f550-\U0001f579\U0001f57b-\U0001f5a3\U0001f5a5-\U0001f642\U0001f645-\U0001f6cf\U0001f6e0-\U0001f6ec\U0001f6f0-\U0001f6f3\U0001f700-\U0001f773\U0001f780-\U0001f7d4\U0001f800-\U0001f80b\U0001f810-\U0001f847\U0001f850-\U0001f859\U0001f860-\U0001f887\U0001f890-\U0001f8ad];
N = [\x30-\x39\xb2-\xb3\xb9-\xb9\xbc-\xbe\u0660-\u0669\u06f0-\u06f9\u07c0-\u07c9\u0966-\u096f\u09e6-\u09ef\u09f4-\u09f9\u0a66-\u0a6f\u0ae6-\u0aef\u0b66-\u0b6f\u0b72-\u0b77\u0be6-\u0bf2\u0c66-\u0c6f\u0c78-\u0c7e\u0ce6-\u0cef\u0d66-\u0d75\u0de6-\u0def\u0e50-\u0e59\u0ed0-\u0ed9\u0f20-\u0f33\u1040-\u1049\u1090-\u1099\u1369-\u137c\u16ee-\u16f0\u17e0-\u17e9\u17f0-\u17f9\u1810-\u1819\u1946-\u194f\u19d0-\u19da\u1a80-\u1a89\u1a90-\u1a99\u1b50-\u1b59\u1bb0-\u1bb9\u1c40-\u1c49\u1c50-\u1c59\u2070-\u2070\u2074-\u2079\u2080-\u2089\u2150-\u2182\u2185-\u2189\u2460-\u249b\u24ea-\u24ff\u2776-\u2793\u2cfd-\u2cfd\u3007-\u3007\u3021-\u3029\u3038-\u303a\u3192-\u3195\u3220-\u3229\u3248-\u324f\u3251-\u325f\u3280-\u3289\u32b1-\u32bf\ua620-\ua629\ua6e6-\ua6ef\ua830-\ua835\ua8d0-\ua8d9\ua900-\ua909\ua9d0-\ua9d9\ua9f0-\ua9f9\uaa50-\uaa59\uabf0-\uabf9\uff10-\uff19\U00010107-\U00010133\U00010140-\U00010178\U0001018a-\U0001018b\U000102e1-\U000102fb\U00010320-\U00010323\U00010341-\U00010341\U0001034a-\U0001034a\U000103d1-\U000103d5\U000104a0-\U000104a9\U00010858-\U0001085f\U00010879-\U0001087f\U000108a7-\U000108af\U00010916-\U0001091b\U00010a40-\U00010a47\U00010a7d-\U00010a7e\U00010a9d-\U00010a9f\U00010aeb-\U00010aef\U00010b58-\U00010b5f\U00010b78-\U00010b7f\U00010ba9-\U00010baf\U00010e60-\U00010e7e\U00011052-\U0001106f\U000110f0-\U000110f9\U00011136-\U0001113f\U000111d0-\U000111d9\U000111e1-\U000111f4\U000112f0-\U000112f9\U000114d0-\U000114d9\U00011650-\U00011659\U000116c0-\U000116c9\U000118e0-\U000118f2\U00012400-\U0001246e\U00016a60-\U00016a69\U00016b50-\U00016b59\U00016b5b-\U00016b61\U0001d360-\U0001d371\U0001d7ce-\U0001d7ff\U0001e8c7-\U0001e8cf\U0001f100-\U0001f10c];
Nl = [\u16ee-\u16f0\u2160-\u2182\u2185-\u2188\u3007-\u3007\u3021-\u3029\u3038-\u303a\ua6e6-\ua6ef\U00010140-\U00010174\U00010341-\U00010341\U0001034a-\U0001034a\U000103d1-\U000103d5\U00012400-\U0001246e];
Pc = [\x5f-\x5f\u203f-\u2040\u2054-\u2054\ufe33-\ufe34\ufe4d-\ufe4f\uff3f-\uff3f];

// Sm categorized by operator precedence
Sm_id     = [\u03f6\u2202\u2205-\u2207\u221e\u223f\u22ee-\u22f1\u25b7\u25c1\u25f8-\u25ff\u27c0\u27c1\u29b0-\u29b5\u29bd\u29c4\u29c5\u29c8-\u29cd\u29d6\u29d7\u29dd\u29de\u29e0\u29e8-\u29f3]; // ϶ ∂ ∅ ∆ ∇ ∞ ∿ ⋮ ⋯ ⋰ ⋱ ▷ ◁ ◸ ◹ ◺ ◻ ◼ ◽ ◾ ◿ ⟀ ⟁ ⦰ ⦱ ⦲ ⦳ ⦴ ⦵ ⦽ ⧄ ⧅ ⧈ ⧉ ⧊ ⧋ ⧌ ⧍ ⧖ ⧗ ⧝ ⧞ ⧠ ⧨ ⧩ ⧪ ⧫ ⧬ ⧭ ⧮ ⧯ ⧰ ⧱ ⧲ ⧳
Sm_nfkc   = [\u2044\u207a-\u207c\u208a-\u208c\u2140\u2212\u2215\u2216\u2223\u2224\u222c\u222d\u222f\u2230\u2236\u223c\u2241\u2a0c\u2a74-\u2a76\ufb29\ufe62\ufe64-\ufe66\uff0b\uff1c-\uff1e\uff5c\uff5e\uffe2\uffe9-\uffec]; // ⁄ ⁺ ⁻ ⁼ ₊ ₋ ₌ ⅀ − ∕ ∖ ∣ ∤ ∬ ∭ ∯ ∰ ∶ ∼ ≁ ⨌ ⩴ ⩵ ⩶ ﬩ ﹢ ﹤ ﹥ ﹦ ＋ ＜ ＝ ＞ ｜ ～ ￢ ￩ ￪ ￫ ￬
Sm_norm   = [\u0606\u0607\u2052\u2118\u2141-\u2144\u214b\u220a\u220d\u2217\u223d\u223e\u229d\u22f4\u22f7\u22fc\u22fe\u27c2\u27cb\u27cd\u27d8\u27d9\u27dd\u27de\u2980\u2982\u29f5\u29f8\u29f9\u2a1f\u2a3e\u2ade-\u2ae0]; // ؆ ؇ ⁒ ℘ ⅁ ⅂ ⅃ ⅄ ⅋ ∊ ∍ ∗ ∽ ∾ ⊝ ⋴ ⋷ ⋼ ⋾ ⟂ ⟋ ⟍ ⟘ ⟙ ⟝ ⟞ ⦀ ⦂ ⧵ ⧸ ⧹ ⨟ ⨾ ⫞ ⫟ ⫠
Sm_unop   = [\u221a-\u221c]; // √ ∛ ∜
Sm_comp   = [\u2218\u229a\u22c6\u29be\u29c7]; // ∘ ⊚ ⋆ ⦾ ⧇
Sm_produ  = [\u220f\u22c2\u2a00\u2a02\u2a05\u2a09]; // ∏ ⋂ ⨀ ⨂ ⨅ ⨉
Sm_prodb  = [\xd7\u2219\u2229\u2240\u2293\u2297\u2299\u229b\u22a0\u22a1\u22c4\u22c5\u22c7-\u22cc\u22d2\u27d0\u27d5-\u27d7\u27e1\u2981\u29bb\u29bf\u29c6\u29d1-\u29d5\u29e2\u2a1d\u2a2f-\u2a37\u2a3b-\u2a3d\u2a40\u2a43\u2a44\u2a4b\u2a4d\u2a4e]; // × ∙ ∩ ≀ ⊓ ⊗ ⊙ ⊛ ⊠ ⊡ ⋄ ⋅ ⋇ ⋈ ⋉ ⋊ ⋋ ⋌ ⋒ ⟐ ⟕ ⟖ ⟗ ⟡ ⦁ ⦻ ⦿ ⧆ ⧑ ⧒ ⧓ ⧔ ⧕ ⧢ ⨝ ⨯ ⨰ ⨱ ⨲ ⨳ ⨴ ⨵ ⨶ ⨷ ⨻ ⨼ ⨽ ⩀ ⩃ ⩄ ⩋ ⩍ ⩎
Sm_divu   = [\u2210]; // ∐
Sm_divb   = [\xf7\u2298\u27cc\u29b8\u29bc\u29f6\u29f7\u2a38\u2afb\u2afd]; // ÷ ⊘ ⟌ ⦸ ⦼ ⧶ ⧷ ⨸ ⫻ ⫽
Sm_sumu   = [\u2211\u222b\u222e\u2231-\u2233\u22c3\u2a01\u2a03\u2a04\u2a06\u2a0a\u2a0b\u2a0d-\u2a1c\u2aff]; // ∑ ∫ ∮ ∱ ∲ ∳ ⋃ ⨁ ⨃ ⨄ ⨆ ⨊ ⨋ ⨍ ⨎ ⨏ ⨐ ⨑ ⨒ ⨓ ⨔ ⨕ ⨖ ⨗ ⨘ ⨙ ⨚ ⨛ ⨜ ⫿
Sm_sumb   = [\x2b\x7e\xac\xb1\u2213\u2214\u222a\u2238-\u223b\u2242\u228c-\u228e\u2294-\u2296\u229e\u229f\u22b9\u22bb\u22d3\u29fa\u29fb\u29fe\u29ff\u2a22-\u2a2e\u2a39\u2a3a\u2a3f\u2a41\u2a42\u2a45\u2a4a\u2a4c\u2a4f\u2a50\u2a6a\u2a6b\u2aec\u2aed\u2afe]; // + ~ ¬ ± ∓ ∔ ∪ ∸ ∸ ∹ ∺ ∻ ≂ ⊌ ⊍ ⊎ ⊔ ⊕ ⊖ ⊞ ⊟ ⊹ ⊻ ⋓ ⧺ ⧻ ⧾ ⧿ ⨢ ⨣ ⨤ ⨥ ⨦ ⨧ ⨨ ⨩ ⨪ ⨫ ⨬ ⨭ ⨮ ⨹ ⨺ ⨿ ⩁ ⩂ ⩅ ⩊ ⩌ ⩏ ⩐ ⩪ ⩫ ⫬ ⫭ ⫾
Sm_lt     = [\x3c\u2264\u2266\u2268\u226a\u226e\u2270\u2272\u2274\u2276\u2278\u227a\u227c\u227e\u2280\u2282\u2284\u2286\u2288\u228a\u228f\u2291\u22b0\u22b2\u22b4\u22b7\u22d0\u22d6\u22d8\u22da\u22dc\u22de\u22e0\u22e2\u22e4\u22e6\u22e8\u22ea\u22ec\u27c3\u27c8\u29c0\u29cf\u29e1\u2a79\u2a7b\u2a7d\u2a7f\u2a81\u2a83\u2a85\u2a87\u2a89\u2a8b\u2a8d\u2a8f\u2a91\u2a93\u2a95\u2a97\u2a99\u2a9b\u2a9d\u2a9f\u2aa1\u2aa3\u2aa6\u2aa8\u2aaa\u2aac\u2aaf\u2ab1\u2ab3\u2ab5\u2ab7\u2ab9\u2abb\u2abd\u2abf\u2ac1\u2ac3\u2ac5\u2ac7\u2ac9\u2acb\u2acd\u2acf\u2ad1\u2ad3\u2ad5\u2af7\u2af9]; // < ≤ ≦ ≨ ≪ ≮ ≰ ≲ ≴ ≶ ≸ ≺ ≼ ≾ ⊀ ⊂ ⊄ ⊆ ⊈ ⊊ ⊏ ⊑ ⊰ ⊲ ⊴ ⊷ ⋐ ⋖ ⋘ ⋚ ⋜ ⋞ ⋠ ⋢ ⋤ ⋦ ⋨ ⋪ ⋬ ⟃ ⟈ ⧀ ⧏ ⧡ ⩹ ⩻ ⩽ ⩿ ⪁ ⪃ ⪅ ⪇ ⪉ ⪋ ⪍ ⪏ ⪑ ⪓ ⪕ ⪗ ⪙ ⪛ ⪝ ⪟ ⪡ ⪣ ⪦ ⪨ ⪪ ⪬ ⪯ ⪱ ⪳ ⪵ ⪷ ⪹ ⪻ ⪽ ⪿ ⫁ ⫃ ⫅ ⫇ ⫉ ⫋ ⫍ ⫏ ⫑ ⫓ ⫕ ⫷ ⫹
Sm_gt     = [\x3e\u2265\u2267\u2269\u226b\u226f\u2271\u2273\u2275\u2277\u2279\u227b\u227d\u227f\u2281\u2283\u2285\u2287\u2289\u228b\u2290\u2292\u22b1\u22b3\u22b5\u22b6\u22d1\u22d7\u22d9\u22db\u22dd\u22df\u22e1\u22e3\u22e5\u22e7\u22e9\u22eb\u22ed\u27c4\u27c9\u29c1\u29d0\u2a7a\u2a7c\u2a7e\u2a80\u2a82\u2a84\u2a86\u2a88\u2a8a\u2a8c\u2a8e\u2a90\u2a92\u2a94\u2a96\u2a98\u2a9a\u2a9c\u2a9e\u2aa0\u2aa2\u2aa7\u2aa9\u2aab\u2aad\u2ab0\u2ab2\u2ab4\u2ab6\u2ab8\u2aba\u2abc\u2abe\u2ac0\u2ac2\u2ac4\u2ac6\u2ac8\u2aca\u2acc\u2ace\u2ad0\u2ad2\u2ad4\u2ad6\u2af8\u2afa]; // > ≥ ≧ ≩ ≫ ≯ ≱ ≳ ≵ ≷ ≹ ≻ ≽ ≿ ⊁ ⊃ ⊅ ⊇ ⊉ ⊋ ⊐ ⊒ ⊱ ⊳ ⊵ ⊶ ⋑ ⋗ ⋙ ⋛ ⋝ ⋟ ⋡ ⋣ ⋥ ⋧ ⋩ ⋫ ⋭ ⟄ ⟉ ⧁ ⧐ ⩺ ⩼ ⩾ ⪀ ⪂ ⪄ ⪆ ⪈ ⪊ ⪌ ⪎ ⪐ ⪒ ⪔ ⪖ ⪘ ⪚ ⪜ ⪞ ⪠ ⪢ ⪧ ⪩ ⪫ ⪭ ⪰ ⪲ ⪴ ⪶ ⪸ ⪺ ⪼ ⪾ ⫀ ⫂ ⫄ ⫆ ⫈ ⫊ ⫌ ⫎ ⫐ ⫒ ⫔ ⫖ ⫸ ⫺
Sm_eq     = [\x3d\u2243-\u2263\u226d\u229c\u22cd\u22d5\u29c2\u29c3\u29ce\u29e3-\u29e7\u2a46-\u2a49\u2a59\u2a66-\u2a69\u2a6c-\u2a73\u2a77\u2a78\u2aa4\u2aa5\u2aae\u2ad7\u2ad8]; // = ≃ ≄ ≅ ≆ ≇ ≈ ≉ ≊ ≋ ≌ ≍ ≎ ≏ ≐ ≑ ≒ ≓ ≔ ≕ ≖ ≗ ≘ ≙ ≚ ≛ ≜ ≝ ≞ ≟ ≠ ≡ ≢ ≣ ≭ ⊜ ⋍ ⋕ ⧂ ⧃ ⧎ ⧣ ⧤ ⧥ ⧦ ⧧ ⩆ ⩇ ⩈ ⩉ ⩙ ⩦ ⩧ ⩨ ⩩ ⩬ ⩭ ⩮ ⩯ ⩰ ⩱ ⩲ ⩳ ⩷ ⩸ ⪤ ⪥ ⪮ ⫗ ⫘
Sm_test   = [\u2208\u2209\u220b\u220c\u221d\u221f-\u2222\u2225\u2226\u226c\u22be\u22bf\u22d4\u22f2\u22f3\u22f5\u22f6\u22f8-\u22fb\u22fd\u22ff\u237c\u27ca\u27d2\u299b-\u29af\u29b6\u29b7\u29b9\u29ba\u2a64\u2a65\u2ad9-\u2add\u2ae1\u2aee\u2af2-\u2af6\u2afc]; // ∈ ∉ ∋ ∌ ∝ ∟ ∠ ∡ ∢ ∥ ∦ ≬ ⊾ ⊿ ⋔ ⋲ ⋳ ⋵ ⋶ ⋸ ⋹ ⋺ ⋻ ⋽ ⋿ ⍼ ⟊ ⟒ ⦛ ⦜ ⦝ ⦞ ⦟ ⦠ ⦡ ⦢ ⦣ ⦤ ⦥ ⦦ ⦧ ⦨ ⦩ ⦪ ⦫ ⦬ ⦭ ⦮ ⦯ ⦶ ⦷ ⦹ ⦺ ⩤ ⩥ ⫙ ⫚ ⫛ ⫝̸ ⫝ ⫡ ⫮ ⫲ ⫳ ⫴ ⫵ ⫶ ⫼
Sm_andu   = [\u22c0]; // ⋀
Sm_andb   = [\u2227\u22bc\u22cf\u27ce\u27d1\u2a07\u2a51\u2a53\u2a55\u2a58\u2a5a\u2a5c\u2a5e-\u2a60]; // ∧ ⊼ ⋏ ⟎ ⟑ ⨇ ⩑ ⩓ ⩕ ⩘ ⩚ ⩜ ⩞ ⩞ ⩟ ⩟ ⩠ ⩠
Sm_oru    = [\u22c1]; // ⋁
Sm_orb    = [\x7c\u2228\u22bd\u22ce\u27c7\u27cf\u2a08\u2a52\u2a54\u2a56\u2a57\u2a5b\u2a5d\u2a61-\u2a63]; // | ∨ ⊽ ⋎ ⟇ ⟏ ⨈ ⩒ ⩔ ⩖ ⩗ ⩛ ⩝ ⩡ ⩢ ⩣
Sm_Sc     = [\u266f]; // ♯
Sm_larrow = [\u2190\u2191\u219a\u21f7\u21fa\u21fd\u22a3\u22a5\u27e3\u27e5\u27f0\u27f2\u27f5\u27f8\u27fb\u27fd\u2902\u2906\u2909\u290a\u290c\u290e\u2912\u2919\u291b\u291d\u291f\u2923\u2926\u2927\u292a\u2931\u2932\u2934\u2936\u293a\u293d\u293e\u2940\u2943\u2944\u2946\u2949\u2952\u2954\u2956\u2958\u295a\u295c\u295e\u2960\u2962\u2963\u296a\u296b\u2973\u2976\u2977\u297a-\u297c\u297e\u2ae3-\u2ae5\u2ae8\u2aeb\u2b30-\u2b42\u2b49-\u2b4b]; // ← ↑ ↚ ⇷ ⇺ ⇽ ⊣ ⊥ ⟣ ⟥ ⟰ ⟲ ⟵ ⟸ ⟻ ⟽ ⤂ ⤆ ⤉ ⤊ ⤌ ⤎ ⤒ ⤙ ⤛ ⤝ ⤟ ⤣ ⤦ ⤧ ⤪ ⤱ ⤲ ⤴ ⤶ ⤺ ⤽ ⤾ ⥀ ⥃ ⥄ ⥆ ⥉ ⥒ ⥔ ⥖ ⥘ ⥚ ⥜ ⥞ ⥠ ⥢ ⥣ ⥪ ⥫ ⥳ ⥶ ⥷ ⥺ ⥻ ⥼ ⥾ ⫣ ⫤ ⫥ ⫨ ⫫ ⬰ ⬱ ⬲ ⬳ ⬴ ⬵ ⬶ ⬷ ⬸ ⬹ ⬺ ⬻ ⬼ ⬽ ⬾ ⬿ ⭀ ⭁ ⭂ ⭉ ⭊ ⭋
Sm_rarrow = [\u2192\u2193\u219b\u21a0\u21a3\u21a6\u21cf\u21d2\u21f4\u21f6\u21f8\u21fb\u21fe\u22a2\u22a4\u22a6-\u22af\u22ba\u27e2\u27e4\u27f1\u27f3\u27f4\u27f6\u27f9\u27fc\u27fe\u27ff\u2900\u2901\u2903\u2905\u2907\u2908\u290b\u290d\u290f-\u2911\u2913-\u2918\u291a\u291c\u291e\u2920\u2924\u2925\u2928\u2929\u292d-\u2930\u2933\u2935\u2937-\u2939\u293b\u293c\u293f\u2941\u2942\u2945\u2947\u2953\u2955\u2957\u2959\u295b\u295d\u295f\u2961\u2964\u2965\u296c\u296d\u2970-\u2972\u2974\u2975\u2978\u2979\u297d\u297f\u29f4\u2ae2\u2ae6\u2ae7\u2aea\u2b43\u2b44\u2b47\u2b48\u2b4c]; // → ↓ ↛ ↠ ↣ ↦ ⇏ ⇒ ⇴ ⇶ ⇸ ⇻ ⇾ ⊢ ⊤ ⊦ ⊧ ⊨ ⊩ ⊪ ⊫ ⊬ ⊭ ⊮ ⊯ ⊺ ⟢ ⟤ ⟱ ⟳ ⟴ ⟶ ⟹ ⟼ ⟾ ⟿ ⤀ ⤁ ⤃ ⤅ ⤇ ⤈ ⤋ ⤍ ⤏ ⤐ ⤑ ⤓ ⤔ ⤕ ⤖ ⤗ ⤘ ⤚ ⤜ ⤞ ⤠ ⤤ ⤥ ⤨ ⤩ ⤭ ⤮ ⤯ ⤰ ⤳ ⤵ ⤷ ⤸ ⤹ ⤻ ⤼ ⤿ ⥁ ⥂ ⥅ ⥇ ⥓ ⥕ ⥗ ⥙ ⥛ ⥝ ⥟ ⥡ ⥤ ⥥ ⥬ ⥭ ⥰ ⥱ ⥲ ⥴ ⥵ ⥸ ⥹ ⥽ ⥿ ⧴ ⫢ ⫦ ⫧ ⫪ ⭃ ⭄ ⭇ ⭈ ⭌
Sm_earrow = [\u2194\u21ae\u21ce\u21d4\u21f5\u21f9\u21fc\u21ff\u27da\u27db\u27e0\u27f7\u27fa\u2904\u2921\u2922\u292b\u292c\u2948\u294a-\u2951\u2966-\u2969\u296e\u296f\u2ae9]; // ↔ ↮ ⇎ ⇔ ⇵ ⇹ ⇼ ⇿ ⟚ ⟛ ⟠ ⟷ ⟺ ⤄ ⤡ ⤢ ⤫ ⤬ ⥈ ⥊ ⥋ ⥌ ⥍ ⥎ ⥏ ⥐ ⥑ ⥦ ⥧ ⥨ ⥩ ⥮ ⥯ ⫩
Sm_quant  = [\u2200\u2201\u2203\u2204\u220e\u2234\u2235\u2237]; // ∀ ∁ ∃ ∄ ∎ ∴ ∵ ∷
Sm_wtf    = [\u0608\u22b8\u27d3\u27d4\u27dc\u27df\u2999\u299a\u29dc\u29df\u2a1e\u2a20\u2a21\u2aef-\u2af1]; // ؈ ⊸ ⟓ ⟔ ⟜ ⟟ ⦙ ⦚ ⧜ ⧟ ⨞ ⨠ ⨡ ⫯ ⫰ ⫱
Sm_multi  = [\u2320\u2321\u237c\u239b-\u23b3\u23dc-\u23e1]; // ⌠ ⌡ ⍼ ⎛ ⎜ ⎝ ⎞ ⎟ ⎠ ⎡ ⎢ ⎣ ⎤ ⎥ ⎦ ⎧ ⎨ ⎩ ⎪ ⎫ ⎬ ⎭ ⎮ ⎯ ⎰ ⎱ ⎲ ⎳ ⏜ ⏝ ⏞ ⏟ ⏠ ⏡
Sm_op = Sm_nfkc | Sm_norm | Sm_unop | Sm_comp | Sm_produ | Sm_prodb | Sm_divu | Sm_divb | Sm_sumu | Sm_sumb | Sm_lt | Sm_gt | Sm_eq | Sm_test | Sm_andu | Sm_andb | Sm_oru | Sm_orb | Sm_Sc | Sm_larrow | Sm_rarrow | Sm_earrow | Sm_quant;
*/

struct input_t {
  unsigned char buf[SIZE + YYMAXFILL];
  const unsigned char *lim;
  const unsigned char *cur;
  const unsigned char *mar;
  const unsigned char *tok;
  const unsigned char *sol;
  /*!stags:re2c format = "const unsigned char *@@;"; */
  long offset;
  int  row;
  bool eof;

  const char *filename;
  FILE *const file;

  input_t(const char *fn, FILE *f, int start = SIZE, int end = SIZE)
   : buf(), lim(buf + end), cur(buf + start), mar(buf + start), tok(buf + start), sol(buf + start),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(-start), row(1), eof(false), filename(fn), file(f) { }
  input_t(const char *fn, const unsigned char *buf_, int end)
   : buf(), lim(buf_ + end), cur(buf_), mar(buf_), tok(buf_), sol(buf_),
     /*!stags:re2c format = "@@(NULL)"; separator = ","; */,
     offset(0), row(1), eof(false), filename(fn), file(0) { }

  bool __attribute__ ((noinline)) fill(size_t need);

  Coordinates coord() const { return Coordinates(row, 1 + cur - sol, offset + cur - &buf[0]); }
};

#define SYM_LOCATION Location(in.filename, start, in.coord()-1)
#define mkSym2(x, v) Symbol(x, SYM_LOCATION, v)
#define mkSym(x) Symbol(x, SYM_LOCATION)

bool input_t::fill(size_t need) {
  if (eof) {
    return false;
  }

  const size_t used = lim - tok;
  const size_t free = SIZE - used;
  if (SIZE < need+used) {
    return false;
  }

  memmove(buf, tok, used);
  const unsigned char *newlim = buf + used;
  offset += free;

  cur = newlim - (lim - cur);
  mar = newlim - (lim - mar);
  tok = newlim - (lim - tok);
  sol = newlim - (lim - sol);
  /*!stags:re2c format = "if (@@) @@ = newlim - (lim - @@);"; */

  lim = newlim;
  if (file) lim += fread(buf + (lim - buf), 1, free, file);

  if (lim < buf + SIZE) {
    eof = true;
    memset(buf + (lim - buf), 0, YYMAXFILL);
    lim += YYMAXFILL;
  }

  return true;
}

static ssize_t unicode_escape(const unsigned char *s, const unsigned char *e, char **out, bool compat) {
  utf8proc_uint8_t *dst;
  ssize_t len;

  utf8proc_option_t oCanon = static_cast<utf8proc_option_t>(
      UTF8PROC_COMPOSE   |
      UTF8PROC_IGNORE    |
      UTF8PROC_LUMP      |
      UTF8PROC_REJECTNA);
  utf8proc_option_t oCompat = static_cast<utf8proc_option_t>(
      UTF8PROC_COMPAT    |
      oCanon);

  len = utf8proc_map(
    reinterpret_cast<const utf8proc_uint8_t*>(s),
    e - s,
    &dst,
    compat?oCompat:oCanon);

  *out = reinterpret_cast<char*>(dst);
  return len;
}

static std::string unicode_escape_canon(std::string &&str) {
  char *cleaned;
  const unsigned char *data = reinterpret_cast<const unsigned char *>(str.data());
  ssize_t len = unicode_escape(data, data + str.size(), &cleaned, false);
  if (len < 0) return std::move(str);
  std::string out(cleaned, len);
  free(cleaned);
  return out;
}

static bool lex_rstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  std::string slice;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;
        *                    { return false; }
        "\\`"                { slice.push_back('`'); continue; }
        "`"                  { break; }
        [^\x00]              { slice.append(in.tok, in.cur); continue; }
    */
  }

  std::shared_ptr<RegExp> exp = std::make_shared<RegExp>(unicode_escape_canon(std::move(slice)));
  if (!exp->exp.ok()) {
    lex.fail = true;
    std::cerr << "Invalid regular expression at "
      << SYM_LOCATION.file() << "; "
      << exp->exp.error() << std::endl;
  }
  out = new Literal(SYM_LOCATION, std::move(exp));
  return true;
}

static bool lex_sstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  std::string slice;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;
        *                    { slice.push_back(*in.tok); continue; }
        [\x00]               { return false; }
        "'"                  { break; }
    */
  }

  // NOTE: unicode_escape NOT invoked; '' is raw "" is cleaned
  std::shared_ptr<String> str = std::make_shared<String>(std::move(slice));
  out = new Literal(SYM_LOCATION, std::move(str));
  return true;
}

static bool lex_dstr(Lexer &lex, Expr *&out)
{
  input_t &in = *lex.engine.get();
  Coordinates start = in.coord() - 1;
  std::vector<Expr*> exprs;
  std::string slice;
  bool ok = true;

  while (true) {
    in.tok = in.cur;
    /*!re2c
        re2c:define:YYCURSOR = in.cur;
        re2c:define:YYMARKER = in.mar;
        re2c:define:YYLIMIT = in.lim;
        re2c:yyfill:enable = 1;
        re2c:define:YYFILL = "if (!in.fill(@@)) return false;";
        re2c:define:YYFILL:naked = 1;

        * { return false; }
        [{] {
          std::shared_ptr<String> str = std::make_shared<String>(std::move(slice));
          exprs.push_back(new Literal(SYM_LOCATION, std::move(str)));
          lex.consume();
          exprs.push_back(parse_expr(lex));
          if (lex.next.type == EOL) lex.consume();
          expect(BCLOSE, lex);
          start = in.coord();
          continue;
        }

        ["]                  { break; }
        [^\n\\\x00]          { slice.append(in.tok, in.cur); continue; }
        "\\{"                { slice.push_back('{');  continue; }
        "\\}"                { slice.push_back('}');  continue; }
        "\\a"                { slice.push_back('\a'); continue; }
        "\\b"                { slice.push_back('\b'); continue; }
        "\\f"                { slice.push_back('\f'); continue; }
        "\\n"                { slice.push_back('\n'); continue; }
        "\\r"                { slice.push_back('\r'); continue; }
        "\\t"                { slice.push_back('\t'); continue; }
        "\\v"                { slice.push_back('\v'); continue; }
        "\\\\"               { slice.push_back('\\'); continue; }
        "\\'"                { slice.push_back('\''); continue; }
        "\\\""               { slice.push_back('"');  continue; }
        "\\?"                { slice.push_back('?');  continue; }
        "\\"  [0-7]{1,3}     { ok &= push_utf8(slice, lex_oct(in.tok, in.cur)); continue; }
        "\\x" [0-9a-fA-F]{2} { ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\u" [0-9a-fA-F]{4} { ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
        "\\U" [0-9a-fA-F]{8} { ok &= push_utf8(slice, lex_hex(in.tok, in.cur)); continue; }
    */
  }

  std::shared_ptr<String> str = std::make_shared<String>(unicode_escape_canon(std::move(slice)));
  exprs.push_back(new Literal(SYM_LOCATION, std::move(str)));

  if (exprs.size() == 1) {
    out = exprs.front();
  } else {
    Expr *cat = new Prim(LOCATION, "catopen");
    for (auto expr : exprs)
      cat = new App(expr->location, new App(LOCATION, new VarRef(LOCATION, "_ catadd"), cat), expr);
    cat = new App(LOCATION, new Lambda(LOCATION, "_", new Prim(LOCATION, "catclose")), cat);
    cat = new App(LOCATION, new Lambda(LOCATION, "_ catadd", cat),
            new Lambda(LOCATION, "_", new Lambda(LOCATION, "_", new Prim(LOCATION, "catadd"))));
    out = cat;
  }

  return ok;
}

static Symbol lex_top(Lexer &lex) {
  input_t &in = *lex.engine.get();
  Coordinates start;
top:
  start = in.coord();
  in.tok = in.cur;

  /*!re2c
      re2c:define:YYCURSOR = in.cur;
      re2c:define:YYMARKER = in.mar;
      re2c:define:YYLIMIT = in.lim;
      re2c:yyfill:enable = 1;
      re2c:define:YYFILL = "if (!in.fill(@@)) return mkSym(ERROR);";
      re2c:define:YYFILL:naked = 1;

      end = "\x00";

      *   { return mkSym(ERROR); }
      end { return mkSym((in.lim - in.tok == YYMAXFILL) ? END : ERROR); }

      nl = [\n\v\f\r\x85\u2028\u2029] | "\r\n";
      notnl = [^\n\v\f\r\x85\u2028\u2029\x00];
      lws = [\t \xa0\u1680\u2000-\u200A\u202F\u205F\u3000];

      // whitespace
      lws+               { goto top; }
      "#" notnl*         { goto top; }
      nl lws* / ("#"|nl) { ++in.row; in.sol = in.tok+1; goto top; }
      nl lws*            { ++in.row; in.sol = in.tok+1; return mkSym(EOL); }

      // character and string literals
      [`] { Expr *out = 0; bool ok = lex_rstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }
      ['] { Expr *out = 0; bool ok = lex_sstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }
      ["] { Expr *out = 0; bool ok = lex_dstr(lex, out); return mkSym2(ok ? LITERAL : ERROR, out); }

      // double literals
      dec = [1-9][0-9_]*;
      double10  = (dec|"0") "." [0-9_]+ ([eE] [+-]? [0-9_]+)?;
      double10e = (dec|"0") [eE] [+-]? [0-9_]+;
      double16  = "0x" [0-9a-fA-F_]+ "." [0-9a-fA-F_]+ ([pP] [+-]? [0-9a-fA-F_]+)?;
      double16e = "0x" [0-9a-fA-F_]+ [pP] [+-]? [0-9a-fA-F_]+;
      (double10 | double10e | double16 | double16e) {
        std::string x(in.tok, in.cur);
        std::remove(x.begin(), x.end(), '_');
        std::shared_ptr<Double> value = std::make_shared<Double>(x.c_str());
        return mkSym2(LITERAL, new Literal(SYM_LOCATION, std::move(value)));
      }

      // integer literals
      oct = '0'[0-7_]*;
      hex = '0x' [0-9a-fA-F_]+;
      bin = '0b' [01_]+;
      (dec | oct | hex | bin) {
        std::string integer(in.tok, in.cur);
        std::remove(integer.begin(), integer.end(), '_');
        std::shared_ptr<Integer> value = std::make_shared<Integer>(integer.c_str());
        return mkSym2(LITERAL, new Literal(SYM_LOCATION, std::move(value)));
      }

      // keywords
      "def"       { return mkSym(DEF);       }
      "tuple"     { return mkSym(TUPLE);     }
      "data"      { return mkSym(DATA);      }
      "global"    { return mkSym(GLOBAL);    }
      "target"    { return mkSym(TARGET);    }
      "publish"   { return mkSym(PUBLISH);   }
      "subscribe" { return mkSym(SUBSCRIBE); }
      "prim"      { return mkSym(PRIM);      }
      "if"        { return mkSym(IF);        }
      "then"      { return mkSym(THEN);      }
      "else"      { return mkSym(ELSE);      }
      "here"      { return mkSym(HERE);      }
      "match"     { return mkSym(MATCH);     }
      "\\"        { return mkSym(LAMBDA);    }
      "="         { return mkSym(EQUALS);    }
      ":"         { return mkSym(COLON);     }
      "("         { return mkSym(POPEN);     }
      ")"         { return mkSym(PCLOSE);    }
      "{"         { return mkSym(BOPEN);     }
      "}"         { return mkSym(BCLOSE);    }

      // operators
      Po_reserved = [;?@];
      Po_special  = ["#'\\];
      Po_op       = [!%&*,./:];
      // !!! TODO: Po, Pd(without -)
      op = (Sk_notick|Sc|Sm_op|Po_op|"-")+; // [^] is Sk

      // identifiers
      modifier = Lm|M;
      upper = Lt|Lu;
      start = L|So|Sm_id|Nl|"_";
      body = L|So|Sm_id|N|Pc|Lm|M;
      id = modifier* start body*;

      id { return mkSym(ID); }
      op { return mkSym(OPERATOR); }
   */
}

struct state_t {
  std::vector<int> tabs;
  std::string indent;
  bool eol;

  state_t() : tabs(), indent(), eol(false) {
    tabs.push_back(0);
  }
};

Lexer::Lexer(const char *file)
 : engine(new input_t(file, fopen(file, "r"))), state(new state_t), next(ERROR, Location(file, Coordinates(), Coordinates())), fail(false)
{
  if (engine->file) consume();
}

Lexer::Lexer(const std::string &cmdline, const char *target)
  : engine(new input_t(target, reinterpret_cast<const unsigned char *>(cmdline.c_str()), cmdline.size())), state(new state_t), next(ERROR, LOCATION, 0), fail(false)
{
  consume();
}

Lexer::~Lexer() {
  if (engine->file) fclose(engine->file);
}

static std::string op_escape(const char *str) {
  std::string out;
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;

  while (true) {
    const unsigned char *start = s;
    /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *                { break; }
      [\x00]           { break; }

      // Two surrogates => one character in json identifiers
      "\\u" [dD] [89abAB] [0-9a-fA-F]{2} "\\u" [dD] [c-fC-F] [0-9a-fA-F]{2} {
        uint32_t lo = lex_hex(start,   start+6);
        uint32_t hi = lex_hex(start+6, start+12);
        uint32_t x = ((lo & 0x3ff) << 10) + (hi & 0x3ff) + 0x10000;
        push_utf8(out, x);
        continue;
      }
      "\\u" [0-9a-fA-F]{4} { push_utf8(out, lex_hex(start, s)); continue; }

      [\u0606] /* ؆ */ { out.append("∛"); continue; }
      [\u0607] /* ؇ */ { out.append("∜"); continue; }
      [\u2052] /* ⁒ */ { out.append("%"); continue; }
      [\u2118] /* ℘ */ { out.append("P"); continue; }
      [\u2141] /* ⅁ */ { out.append("G"); continue; }
      [\u2142] /* ⅂ */ { out.append("L"); continue; }
      [\u2143] /* ⅃ */ { out.append("L"); continue; }
      [\u2144] /* ⅄ */ { out.append("Y"); continue; }
      [\u214b] /* ⅋ */ { out.append("&"); continue; }
      [\u220a] /* ∊ */ { out.append("∈"); continue; }
      [\u220d] /* ∍ */ { out.append("∋"); continue; }
      [\u2217] /* ∗ */ { out.append("*"); continue; }
      [\u223d] /* ∽ */ { out.append("~"); continue; }
      [\u223e] /* ∾ */ { out.append("~"); continue; }
      [\u229d] /* ⊝ */ { out.append("⊖"); continue; }
      [\u22f4] /* ⋴ */ { out.append("⋳"); continue; }
      [\u22f7] /* ⋷ */ { out.append("⋶"); continue; }
      [\u22fc] /* ⋼ */ { out.append("⋻"); continue; }
      [\u22fe] /* ⋾ */ { out.append("⋽"); continue; }
      [\u27c2] /* ⟂ */ { out.append("⊥"); continue; }
      [\u27cb] /* ⟋ */ { out.append("/"); continue; }
      [\u27cd] /* ⟍ */ { out.append("\\");continue; }
      [\u27d8] /* ⟘ */ { out.append("⊥"); continue; }
      [\u27d9] /* ⟙ */ { out.append("⊤"); continue; }
      [\u27dd] /* ⟝ */ { out.append("⊢"); continue; }
      [\u27de] /* ⟞ */ { out.append("⊣"); continue; }
      [\u2980] /* ⦀ */ { out.append("⫴"); continue; }
      [\u2982] /* ⦂ */ { out.append(":"); continue; }
      [\u29f5] /* ⧵ */ { out.append("\\");continue; }
      [\u29f8] /* ⧸ */ { out.append("/"); continue; }
      [\u29f9] /* ⧹ */ { out.append("\\");continue; }
      [\u2a1f] /* ⨟ */ { out.append(";"); continue; }
      [\u2a3e] /* ⨾ */ { out.append("l"); continue; }
      [\u2ade] /* ⫞ */ { out.append("⋽"); continue; }
      [\u2adf] /* ⫟ */ { out.append("⊤"); continue; }
      [\u2ae0] /* ⫠ */ { out.append("⊥"); continue; }
      [^]              { out.append(
                           reinterpret_cast<const char*>(start),
                           reinterpret_cast<const char*>(s));
                         continue; }
  */}

  return out;
}

std::string Lexer::id() const {
  std::string out;
  char *dst;
  ssize_t len;

  len = unicode_escape(engine->tok, engine->cur, &dst, true); // compat
  if (len >= 0) {
    out = op_escape(dst);
    free(dst);
  } else {
    out.assign(engine->tok, engine->cur);
  }

  return out;
}

void Lexer::consume() {
  if (state->eol) {
    if ((int)state->indent.size() < state->tabs.back()) {
      state->tabs.pop_back();
      next.type = DEDENT;
    } else if ((int)state->indent.size() > state->tabs.back()) {
      state->tabs.push_back(state->indent.size());
      next.type = INDENT;
    } else {
      next.type = EOL;
      state->eol = false;
    }
  } else {
    next = lex_top(*this);
    if (next.type == EOL) {
      std::string newindent(engine->tok+1, engine->cur);
      size_t check = std::min(newindent.size(), state->indent.size());
      if (!std::equal(newindent.begin(), newindent.begin()+check, state->indent.begin())) {
        std::cerr << "Whitespace is neither a prefix nor a suffix of the previous line at " << next.location.file() << std::endl;
        fail = true;
      }
      std::swap(state->indent, newindent);
      state->eol = true;
      consume();
    }
  }
}

bool Lexer::isLower(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;
      *           { return true; }
      "unary "    { return false; }
      "binary "   { return false; }
      modifier    { goto top; }
      "_\x00"     { return false; }
      upper       { return false; }
  */
}

bool Lexer::isUpper(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;
      *           { return false; }
      modifier    { goto top; }
      upper       { return true; }
  */
}

bool Lexer::isOperator(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;
      *           { return false; }
      "unary "    { return true; }
      "binary "   { return true; }
  */
}

op_type op_precedence(const char *str) {
  const unsigned char *s = reinterpret_cast<const unsigned char*>(str);
  const unsigned char *ignore;
  (void)ignore;
top:
  /*!re2c
      re2c:yyfill:enable = 0;
      re2c:define:YYMARKER = ignore;
      re2c:define:YYCURSOR = s;

      *                          { return op_type(-1, -1);}
      "."                        { return op_type(23, 1); }
      [smpa]                     { return op_type(APP_PRECEDENCE, 1); } // SUBSCRIBE/PRIM/APP
      Sm_comp                    { return op_type(21, 0); }
      Sm_unop                    { return op_type(20, 0); }
      "^"                        { return op_type(19, 0); }
      Sm_produ                   { return op_type(18, 0); }
      "*" | Sm_prodb             { return op_type(17, 1); }
      Sm_divu                    { return op_type(16, 0); }
      [/%] | Sm_divb             { return op_type(15, 1); }
      Sm_sumu                    { return op_type(14, 0); }
      [\-] | Sm_sumb             { return op_type(13, 1); }
      Sm_test | Sm_lt | Sm_gt    { return op_type(12, 1); }
      "!" | Sm_eq                { return op_type(11, 0); }
      Sm_andu                    { return op_type(10, 0); }
      "&" | Sm_andb              { return op_type(9, 1);  }
      Sm_oru                     { return op_type(8, 0);  }
      "|" | Sm_orb               { return op_type(7, 1);  }
      Sm_Sc | Sc                 { return op_type(6, 0);  }
      Sm_larrow | Sm_rarrow      { return op_type(5, 1);  }
      Sm_earrow                  { return op_type(4, 0);  }
      Sm_quant                   { return op_type(3, 0);  }
      ":"                        { return op_type(2, 1);  }
      ","                        { return op_type(1, 0);  }
      [i\\]                      { return op_type(0, 0);  } // IF and LAMBDA
      Sk                         { goto top; }
  */
}
