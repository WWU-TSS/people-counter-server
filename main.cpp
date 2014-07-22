#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include "bigmath.h"
#include "stream.h"
#include "network.h"
#include <vector>

using namespace std;

BigUnsigned decryptionModulus = 0_bu;
BigUnsigned decryptionExponent = 0_bu;
const size_t randomBitCount = 64;
const WordType checkSumModulus = 8191;
bool useInfoMessages = false;

void connectionHandler(ReaderIStream & is, WriterOStream & os, string & messages)
{
    string msg;
    char ch;
    while(is.get(ch))
        msg += ch;
    is.close();
    //messages += "msg : \"" + msg + "\"\n";
    if(msg.size() < 1)
    {
        messages += "Error : Invalid request\n";
        os << "0";
        return;
    }
    switch(msg[0])
    {
    case '0': // unencrypted
        msg = msg.substr(1);
        if(decryptionModulus != 0_bu)
        {
            messages += "Error : unencrypted message attempted\n";
            os << "0";
            return;
        }
        break;
    case '1': // encrypted
    {
        msg = msg.substr(1);
        string unencrypted;
        try
        {
            size_t newLineIndex = msg.find_first_of('\n');
            size_t location = 0;
            while(newLineIndex != string::npos)
            {
                BigUnsigned v = BigUnsigned::parseBase64(msg.substr(location, newLineIndex - location));
                location = newLineIndex + 1;
                v = powMod(v, decryptionExponent, decryptionModulus);
                BigUnsigned checkSum;
                BigUnsigned::divMod(v, checkSumModulus, v, checkSum);
                //messages += "checksum : " + checkSum.toString(0x10) + "\n";
                //messages += "message : " + v.toString(0x10) + "\n";
                if(checkSum != v % checkSumModulus)
                    throw runtime_error("checksum doesn't match");
                v >>= randomBitCount;
                unencrypted += v.toByteString();
                newLineIndex = msg.find_first_of('\n', location);
            }
        }
        catch(exception & e)
        {
            messages += string("Error : ") + e.what() + "\n";
            os << "0";
            return;
        }
        msg = unencrypted;
        break;
    }
    default:
        messages += "Error : Invalid encryption type\n";
        os << "0";
        return;
    }
    size_t deviceNameLength = msg.find_first_of('\n');
    if(deviceNameLength == string::npos)
    {
        messages += "Error : can't find device name\n";
        os << "0";
        return;
    }
    string deviceName = msg.substr(0, deviceNameLength);
    msg = msg.substr(deviceNameLength + 1);
    if(useInfoMessages)
        messages += "Info : " + deviceName + " : syncing\n";
    os << "1";
    os.close();
    size_t statsStringLength = msg.find_first_of('\n');
    string statsString = "";
    if(statsStringLength != string::npos)
    {
        statsString = msg.substr(0, statsStringLength);
        msg = msg.substr(statsStringLength + 1);
    }
    string sentTime = statsString;
    vector<pair<time_t, string>> parts;
    while(msg != "")
    {
        size_t newLinePos = msg.find_first_of('\n');
        if(newLinePos == string::npos)
        {
            parts.push_back(make_pair((time_t)0, msg));
            break;
        }
        parts.push_back(make_pair((time_t)0, msg.substr(0, newLinePos)));
        msg = msg.substr(newLinePos + 1);
    }
    for(pair<time_t, string> & part : parts)
    {
        time_t t = time(NULL);
        string str = get<1>(part);
        size_t splitPos = str.find_first_of(' ');
        if(splitPos != string::npos)
        {
            istringstream is(str.substr(0, splitPos));
            is >> hex >> t;
            str = str.substr(splitPos + 1);
        }
        part = make_pair(t, str);
    }
    for(pair<time_t, string> part : parts)
    {
        char str[256];
        strftime(str, sizeof(str), "%c", localtime(&get<0>(part)));
        messages += "Event : " + deviceName + " : " + str + " : " + get<1>(part) + "\n";
    }
}

void connectionThreadFn(shared_ptr<StreamRW> stream, ostream * plogStream)
{
    ReaderIStream is(stream->preader());
    WriterOStream os(stream->pwriter());
    stream = nullptr; // remove reference
    string messages;
    connectionHandler(is, os, messages);
    *plogStream << messages << flush;
}

int main()
{
    ifstream is("dec-key.txt");
    ofstream logFile("/var/www/people-counter-log.txt", ios::app);
    if(is)
    {
        string modulus, exponent;
        is >> modulus >> exponent;
        is.close();
        try
        {
            decryptionExponent = BigUnsigned::parseHexByteString(exponent);
            decryptionModulus = BigUnsigned::parseHexByteString(modulus);
        }
        catch(exception & e)
        {
            cerr << "Error : can't load key from dec-key.txt : " << e.what() << endl;
            return 1;
        }
    }
    else
        cout << "no decryption key loaded\n";
    NetworkServer server(12347);
    for(;;)
    {
        auto connection = server.accept();
        connectionThreadFn(connection, &logFile);
    }
    return 0;
}
