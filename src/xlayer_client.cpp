#include "xlayer_client.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace computemesh::xlayer {
namespace {
constexpr char CPU_COUNT[]="a94da8a7", CPU_AT[]="4bc7cbbd", IS_CPU[]="5f5a364f";
constexpr char INFO[]="084d60f1", NETLIST[]="3fc4be56", EVAL[]="934d06ea", STEP[]="e8281a1a";
std::string strip(const std::string& s){return s.rfind("0x",0)==0?s.substr(2):s;}
std::uint64_t hex(const std::string& s){auto x=strip(s);const auto first=x.find_first_not_of('0');if(first==std::string::npos)return 0;x=x.substr(first);if(x.size()>16||!std::all_of(x.begin(),x.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c));}))throw std::runtime_error("Invalid X Layer hex value: ["+s+"]");return std::stoull(x,nullptr,16);}
std::string word(std::uint64_t v){std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(64)<<v;return s.str();}
std::string hexBytes(const std::vector<std::uint8_t>& v){std::ostringstream s;s<<std::hex<<std::setfill('0');for(auto b:v)s<<std::setw(2)<<static_cast<unsigned>(b);return s.str();}
std::string quote(const std::string& s){std::string o="'";for(char c:s)o+=c=='\''?"'\\''":std::string(1,c);return o+"'";}
std::string result(const std::string& rpc,const std::string& payload,long timeout){if(rpc.find_first_of("'\n\r")!=std::string::npos)throw std::runtime_error("Unsafe X Layer RPC URL");auto cmd="curl --silent --show-error --fail --max-time "+std::to_string(timeout)+" --header 'Content-Type: application/json' --data-raw "+quote(payload)+" "+quote(rpc);FILE* p=popen(cmd.c_str(),"r");if(!p)throw std::runtime_error("Unable to start curl");std::string r;std::array<char,4096>b{};while(fgets(b.data(),static_cast<int>(b.size()),p))r+=b.data();if(pclose(p)!=0)throw std::runtime_error("X Layer RPC request failed or timed out");if(r.find("\"error\"")!=std::string::npos)throw std::runtime_error("X Layer RPC error: "+r);auto m=r.find("\"result\"");auto a=r.find('"',r.find(':',m)+1);auto z=r.find('"',a+1);if(m==std::string::npos||a==std::string::npos||z==std::string::npos)throw std::runtime_error("Malformed X Layer RPC response");return r.substr(a+1,z-a-1);}
std::string rpc(const std::string& method,const std::string& params){return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\""+method+"\",\"params\":["+params+"]}";}
std::string call(const std::string& url,const std::string& to,const std::string& data,const std::string& block,long timeout){return result(url,rpc("eth_call","{\"to\":\""+to+"\",\"data\":\""+data+"\"},\""+block+"\""),timeout);}
std::string addrWord(const std::array<std::uint8_t,20>& a){return std::string(24,'0')+hexBytes(std::vector<std::uint8_t>(a.begin(),a.end()));}
std::vector<std::uint8_t> decodeBytes(const std::string& s){auto x=strip(s);if(x.size()<128||x.size()%2)throw std::runtime_error("Malformed ABI bytes");auto off=hex(x.substr(0,64));auto base=static_cast<std::size_t>(off*2);auto n=hex(x.substr(base,64));auto start=base+64;if(start+n*2>x.size())throw std::runtime_error("Malformed ABI bytes length");std::vector<std::uint8_t>o;for(std::size_t i=0;i<n;++i)o.push_back(static_cast<std::uint8_t>(hex(x.substr(start+i*2,2))));return o;}
std::array<std::uint8_t,20> decodeAddress(const std::string& s){auto x=strip(s);if(x.size()!=64)throw std::runtime_error("Malformed address");std::array<std::uint8_t,20>o{};for(std::size_t i=0;i<20;++i)o[i]=static_cast<std::uint8_t>(hex(x.substr(24+i*2,2)));return o;}
std::string dyn(const std::vector<std::uint8_t>&v){auto p=(32-v.size()%32)%32;return word(32)+word(v.size())+hexBytes(v)+std::string(p*2,'0');}
}
std::string addressHex(const std::array<std::uint8_t,20>& a){return "0x"+hexBytes(std::vector<std::uint8_t>(a.begin(),a.end()));}
Client::Client(std::string r,std::uint64_t c,std::string f,long t):rpcUrl_(std::move(r)),expectedChainId_(c),factoryAddress_(std::move(f)),timeoutSeconds_(t){if(rpcUrl_.empty()||t<=0)throw std::invalid_argument("Invalid X Layer configuration");}
std::uint64_t Client::chainId()const{return hex(result(rpcUrl_,rpc("eth_chainId",""),timeoutSeconds_));}
void Client::validateChain()const{if(chainId()!=expectedChainId_)throw std::runtime_error("X Layer chain ID mismatch");}
std::uint64_t Client::blockNumber()const{return hex(result(rpcUrl_,rpc("eth_blockNumber",""),timeoutSeconds_));}
std::string Client::call(const std::string&to,const std::string&data,const std::string&block)const{return ::computemesh::xlayer::call(rpcUrl_,to,data,block,timeoutSeconds_);}
std::uint64_t Client::cpuCount(const std::string&b)const{return hex(call(factoryAddress_,"0x"+std::string(CPU_COUNT),b));}
std::array<std::uint8_t,20> Client::cpuAt(std::uint64_t i,const std::string&b)const{return decodeAddress(call(factoryAddress_,"0x"+std::string(CPU_AT)+word(i),b));}
bool Client::isCPU(const std::array<std::uint8_t,20>&a,const std::string&b)const{return hex(call(factoryAddress_,"0x"+std::string(IS_CPU)+addrWord(a),b))!=0;}
CircuitInfo Client::circuitInfo(const std::array<std::uint8_t,20>&a,std::uint64_t id,const std::string&b)const{auto x=strip(call(addressHex(a),"0x"+std::string(INFO)+word(id),b));if(x.size()!=256)throw std::runtime_error("Malformed circuitInfo");return {static_cast<std::uint32_t>(hex(x.substr(0,64))),static_cast<std::uint32_t>(hex(x.substr(64,64))),static_cast<std::uint32_t>(hex(x.substr(128,64))),static_cast<std::uint32_t>(hex(x.substr(192,64)))};}
std::vector<std::uint8_t> Client::netlist(const std::array<std::uint8_t,20>&a,std::uint64_t id,const std::string&b)const{return decodeBytes(call(addressHex(a),"0x"+std::string(NETLIST)+word(id),b));}
std::vector<std::uint8_t> Client::eval(const std::array<std::uint8_t,20>&a,std::uint64_t id,const std::vector<std::uint8_t>&in,const std::string&b)const{auto p=(32-in.size()%32)%32;return decodeBytes(call(addressHex(a),"0x"+std::string(EVAL)+word(id)+word(64)+word(in.size())+hexBytes(in)+std::string(p*2,'0'),b));}
Client::StepResult Client::step(const std::array<std::uint8_t,20>&a,std::uint64_t id,const std::vector<std::uint8_t>&state,const std::vector<std::uint8_t>&in,const std::string&b)const{auto x=strip(call(addressHex(a),"0x"+std::string(STEP)+word(id)+word(96)+word(96+((state.size()+31)/32)*32)+dyn(state)+dyn(in),b));auto one=[&](std::size_t p){auto off=hex(x.substr(p,64));auto base=static_cast<std::size_t>(off*2);auto n=hex(x.substr(base,64));auto st=base+64;if(st+n*2>x.size())throw std::runtime_error("Malformed step result");std::vector<std::uint8_t>o;for(std::size_t i=0;i<n;++i)o.push_back(static_cast<std::uint8_t>(hex(x.substr(st+i*2,2))));return o;};if(x.size()<128)throw std::runtime_error("Malformed step result");return {one(0),one(64)};}
}
