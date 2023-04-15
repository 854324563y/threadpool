#include "rebuild-threadpool.h"
#include <iostream>

int main(){
    SimplePool pool{};
    //Threadpool pool(std::thread::hardware_concurrency());

    int n =20;
    for(int i=1;i<=n;++i){
        auto func = [](int j){
            if(j%2 == 1){
                this_thread::sleep_for(0.2s);
            }
            unique_lock<mutex> lock(_m);
            cout << "id : "<<j<<endl;
        };
        pool.enqueue(std::bind(func,i));
        // int j=i;
        // pool.enqueue([](int j){
        //     if(j%2 == 1){
        //         this_thread::sleep_for(0.2s);
        //     }
        //     unique_lock<mutex> lock(_m);
        //     cout << "id : "<<j<<endl;
        // });
    }
}