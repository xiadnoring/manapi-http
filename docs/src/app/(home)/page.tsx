import { loader } from 'fumadocs-core/source';
import Link from 'next/link';
import icon from './icon.png';
import icon_dark from './icon.png';

export default function HomePage() {
  return (
    <main className="flex flex-1 flex-col justify-center text-center">
      <div className="grid sm:grid-cols-1 md:grid-cols-2 container">
        <div className="md:text-start flex flex-1 flex-col justify-center">
          <h2 className="text-[2rem] md:text-[2rem] xl:text-[3rem]">
            <span className="font-bold bg-clip-text text-transparent bg-gradient-to-r from-red-300 to-purple-500">Manapi Http</span>
          </h2>
          <p className="text-[2rem] my-0 md:text-[2rem] my-[-1rem] xl:text-[3rem] font-bold">server/client library for</p>
          <p className="text-[2rem] md:text-[2rem] xl:text-[3rem] font-bold">asynchronous C++</p>
          <div className="text-gray-400 text-[1.2rem] md:text[1.3rem] xl:text-[1.5rem] my-4">
            Build and run microservices written in C++ with different HTTP implementations
          </div>
          <div className="flex gap-2 justify-center md:justify-start">
            <a className="text-white font-medium bg-red-300 transition-colors py-2 px-4 rounded-[1.8rem] bg-gradient-to-r from-red-500 to-purple-500 dark:from-red-400 dark:to-purple-800 hover:bg-red-200" href="./docs/intro/quick-start">Quick Start</a>
            <a className="bg-gray-300 font-medium text-black hover:bg-gray-200 dark:bg-gray-800 transition-colors py-2 px-4 rounded-[1.8rem] dark:hover:bg-gray-700 dark:text-white" href="./docs/intro/install">Documentation</a>
          </div>
        </div>
        <div className="order-[-1] md:order-1 max-h-[350px] flex justify-center items-center">
          <img className="h-full hidden dark:block" src={icon_dark.src}></img>
          <img className="h-full dark:hidden" src={icon.src}></img>
        </div>
      </div>
    </main>
  );
}
