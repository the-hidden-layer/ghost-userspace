import os
import re
import matplotlib.pyplot as plt

def parse_eval_times(file_path):
    eval_times = []
    with open(file_path, 'r') as file:
        for line in file:
            match = re.match(r'\w+EvalTime: (\d+)', line)
            if match:
                eval_times.append(int(match.group(1)))
    return eval_times

def calculate_average(eval_times):
    return sum(eval_times) / len(eval_times)

def main():
    num_tasks_range = range(10, 101, 10)
    averages = []

    for num_tasks in num_tasks_range:
        file_path = f'results/overheadSizeOfTasks/overheadSizeOfTasks_{num_tasks}ms'
        eval_times = parse_eval_times(file_path)
        eval_times = [d/1e6 for d in eval_times]
        avg_eval_time = calculate_average(eval_times)
        averages.append(avg_eval_time)

    # Plotting
    plt.plot(num_tasks_range, averages, marker='o')
    plt.title('ElasticSched: Average Eval Time vs. Size of Tasks')
    plt.xlabel('Size of Tasks (milliseconds)')
    plt.ylabel('Average EvalTime (milliseconds)')
    plt.grid(True)
    # plt.ylim(0, )
    plt.show()

if __name__ == "__main__":
    main()
