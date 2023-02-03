#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>
#include <stack>
#include <fstream>
#include <sstream>
#include <ctime>
#include <chrono>

#include "amd64jit.h"

uint8_t buff[0x20000];

enum op{
    op_add,
    op_sub,
    op_addp,
    op_subp,
    op_jt,
    op_jf,
    op_in,
    op_out
};

struct opcode{
    uint8_t op;
    uint32_t num;
};

std::vector<opcode> scanner(std::string s){
    std::vector<opcode> code;
    std::stack<size_t> stk;
    uint32_t cnt=0;
    int line=0;
    for(size_t i=0;i<s.length();++i){
        switch(s[i]){
            case '+':
                cnt=0;
                while(s[i]=='+'){
                    ++cnt;
                    ++i;
                }
                --i;
                code.push_back({op_add,cnt&0xff});break;
            case '-':
                cnt=0;
                while(s[i]=='-'){
                    ++cnt;
                    ++i;
                }
                --i;
                code.push_back({op_sub,cnt&0xff});break;
            case '>':
                cnt=0;
                while(s[i]=='>'){
                    ++cnt;
                    ++i;
                }
                --i;
                code.push_back({op_addp,cnt});break;
            case '<':
                cnt=0;
                while(s[i]=='<'){
                    ++cnt;
                    ++i;
                }
                --i;
                code.push_back({op_subp,cnt});break;
            case '[':
                stk.push(code.size());
                code.push_back({op_jf,0});break;
            case ']':
                if(stk.empty()){
                    std::cout<<"empty stack at line "<<line<<"\n";
                    std::exit(-1);
                }
                code[stk.top()].num=code.size()&0xffffffff;
                code.push_back({op_jt,(uint32_t)stk.top()});
                stk.pop();break;
            case ',':code.push_back({op_in,0});break;
            case '.':code.push_back({op_out,0});break;
            case '\n':++line;break;
        }
    }
    if(!stk.empty()){
        std::cout<<"lack ]\n";
        std::exit(-1);
    }
    return code;
}

void interpreter(const std::vector<opcode>& code){
    auto begin=std::chrono::high_resolution_clock::now();
    memset(buff,0,sizeof(buff));
    uint32_t p=0;
    for(size_t i=0;i<code.size();++i){
        switch(code[i].op){
            case op_add:buff[p]+=code[i].num;break;
            case op_sub:buff[p]-=code[i].num;break;
            case op_addp:p+=code[i].num;break;
            case op_subp:p-=code[i].num;break;
            case op_jt:if(buff[p])i=code[i].num;break;
            case op_jf:if(!buff[p])i=code[i].num;break;
            case op_in:buff[p]=getchar();break;
            case op_out:putchar(buff[p]);break;
        }
    }
    auto end=std::chrono::high_resolution_clock::now();
    std::cout<<"interpreter time usage: "<<(end-begin).count()*1.0/std::chrono::high_resolution_clock::duration::period::den<<"s\n";
}

void jit(const std::vector<opcode>& code){
    amd64jit mem(65536);
    memset(buff,0,sizeof(buff));

    /* set stack and base pointer */
    mem.push({0x55});// pushq %rbp
    mem.push({0x48,0x89,0xe5});// mov %rsp,%rbp

    /* save register context */
    mem.push({0x57})  // pushq %rdi
        .push({0x56}) // pushq %rsi
        .push({0x53}) // pushq %rbx
        .push({0x52}) // pushq %rdx
        .push({0x51}) // pushq %rcx
        .push({0x50});// pushq %rax

    /* set bf machine's paper pointer */
    mem.push({0x48,0xbb}).push64((uint64_t)buff);// movq $buff,%rbx

    for(auto& op:code){
        switch(op.op){
            case op_add: mem.push({0x80,0x03,(uint8_t)(op.num&0xff)}); break; // addb $op.num,(%rbx)
            case op_sub: mem.push({0x80,0x2b,(uint8_t)(op.num&0xff)}); break; // subb $op.num,(%rbx)
            case op_addp: mem.push({0x48,0x81,0xc3}).push32(op.num); break;   // add $op.num,%rbx
            case op_subp: mem.push({0x48,0x81,0xeb}).push32(op.num); break;   // sub $op.num,%rbx
            case op_jt: // if(al)
                mem.push({0x8a,0x03});// mov (%rbx),%al
                mem.push({0x84,0xc0});// test %al,%al
                mem.jne();
                break;
            case op_jf: // if(!al)
                mem.push({0x8a,0x03});// mov (%rbx),%al
                mem.push({0x84,0xc0});// test %al,%al
                mem.je();
                break;
            case op_in: /* TODO */ break;
            case op_out:
#ifndef _WIN32
                mem.push({0x48,0xb8}).push64((uint64_t)putchar);// movabs $putchar,%rax
                mem.push({0x0f,0xbe,0x3b});// movsbl (%rbx),%edi
                mem.push({0xff,0xd0});// callq *%rax
#else
                mem.push({0x48,0xb8}).push64((uint64_t)putchar);// movabs $putchar,%rax
                mem.push({0x0f,0xbe,0x0b});// movsbl (%rbx),%ecx
                mem.push({0xff,0xd0});// callq *%rax
#endif
                break;
        }
    }

    /* restore register context */
    mem.push({0x58})  // popq %rax
        .push({0x59}) // popq %rcx
        .push({0x5a}) // popq %rdx
        .push({0x5b}) // popq %rbx
        .push({0x5e}) // popq %rsi
        .push({0x5f}) // popq %rdi
        .push({0x5d}) // popq %rbp
        .push({0xc3});// retq

    auto begin=std::chrono::high_resolution_clock::now();
    mem.exec();
    auto end=std::chrono::high_resolution_clock::now();
    std::cout<<"jit-compiler time usage: "<<(end-begin).count()*1.0/std::chrono::high_resolution_clock::duration::period::den<<"s\n";
}

int main(){
    std::ifstream fin("./test/mandelbrot.bf");
    std::stringstream ss;
    ss<<fin.rdbuf();
    std::vector<opcode> code=scanner(ss.str());

    interpreter(code);
    jit(code);
    return 0;
}