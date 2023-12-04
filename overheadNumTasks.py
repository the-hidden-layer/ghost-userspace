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
    num_tasks_range = range(100, 1100, 100)
    averages = []

    for num_tasks in num_tasks_range:
        file_path = f'results/overheadNumTasks/overheadNumTasks_{num_tasks}'
        eval_times = parse_eval_times(file_path)
        print(len(eval_times))
        
        avg_eval_time = calculate_average(eval_times)
        averages.append(avg_eval_time)

    # Plotting
    plt.plot(num_tasks_range, averages, marker='o')
    plt.title('Average EvalTime vs Number of Tasks')
    plt.xlabel('Number of Tasks')
    plt.ylabel('Average EvalTime')
    plt.grid(True)
    plt.show()

if __name__ == "__main__":
    main()
