#include "threadpool.h"
#include <iostream>

int main(){
    Threadpool pool(8);
    //Threadpool pool(std::thread::hardware_concurrency());

    int n =20;
    for(int i=1;i<=n;++i){
        pool.submit([](int id){
            if(id%2 == 1){
                this_thread::sleep_for(0.2s);
            }
            unique_lock<mutex> lock(_m);
            cout << "id : "<<id<<endl;
        }, i);
    }
}